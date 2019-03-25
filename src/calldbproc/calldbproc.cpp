
#include "configuration.h"

#include <unistd.h>
#include <string.h>
#include <sql.h> 
#include <sqlext.h>
#include <openssl/evp.h>

#include <iostream>

#define TRUE 1
#define FALSE 0

using namespace std;
using namespace itfact::common;

Configuration *config;
log4cpp::Category *logger;

SQLHENV hEnv;
SQLHDBC hDbc;
SQLHSTMT hStmt;

SQLCHAR *ODBC_Name = (SQLCHAR*)"my_test";
SQLCHAR *ODBC_ID = (SQLCHAR*)"test";
SQLCHAR *ODBC_PW = (SQLCHAR*)"4641";

BOOL DBConnect();
BOOL DBDistconnect();
BOOL DBExcuteSQL_MIN();
BOOL DBExcuteSQL_SYNC();
BOOL DBExcuteSQL_STAT_JOB();
BOOL DBExcuteSQL_STAT_RES();

const string key = "fooboo1234567890";
const string iv = "fooboo1234567890";

int Encrypt(string &data)
{
    int length=0;
    int key_length, iv_length, data_length;
    key_length = key.size();
    iv_length = iv.size();
    data_length = data.size();

    const EVP_CIPHER *cipher;
    int cipher_key_length, cipher_iv_length;
    cipher = EVP_aes_128_cbc();
    cipher_key_length = EVP_CIPHER_key_length(cipher);
    cipher_iv_length = EVP_CIPHER_iv_length(cipher);

    if (key_length != cipher_key_length || iv_length != cipher_iv_length) {
        return 0;
    }

    EVP_CIPHER_CTX *ctx;
    int i, cipher_length, final_length;
    unsigned char *ciphertext;
    char sByte[3];

    // EVP_CIPHER_CTX_init(&ctx);
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return 0;

    EVP_EncryptInit_ex(ctx, cipher, NULL, (const unsigned char *)key.c_str(), (const unsigned char *)iv.c_str());

    cipher_length = data_length + EVP_MAX_BLOCK_LENGTH;
    ciphertext = (unsigned char *)malloc(cipher_length);

    EVP_EncryptUpdate(ctx, ciphertext, &cipher_length, (const unsigned char *)data.c_str(), data_length);
    EVP_EncryptFinal_ex(ctx, ciphertext + cipher_length, &final_length);

    data.clear();
    for (i = 0; i < cipher_length + final_length; i++)
    {
        sprintf(sByte, "%02X", ciphertext[i]);
        data.append(sByte);
    }
    

    EVP_CIPHER_CTX_free(ctx);

    length = cipher_length + final_length;
    free(ciphertext);
    return length;
}

int Decrypt(string &data)
{
    int key_length, iv_length;
    key_length = key.size();
    iv_length = iv.size();
    int data_length = data.size() / 2;

    const EVP_CIPHER *cipher;
    int cipher_key_length, cipher_iv_length;
    cipher = EVP_aes_128_cbc();
    cipher_key_length = EVP_CIPHER_key_length(cipher);
    cipher_iv_length = EVP_CIPHER_iv_length(cipher);

    if (key_length != cipher_key_length || iv_length != cipher_iv_length) {
        return 0;
    }

    const char *p = data.c_str();;
    unsigned char *datax;
    int datax_length;

    datax = (unsigned char *)malloc(data_length);

    for (int count = 0; count < data_length; count++) {
        sscanf(p, "%2hhx", &datax[count]);
        p += 2;
    }

    datax_length = data_length;

    EVP_CIPHER_CTX *ctx;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        free(datax);
        return 0;
    }

    EVP_DecryptInit_ex(ctx, cipher, NULL, (const unsigned char *)key.c_str(), (const unsigned char *)iv.c_str());

    int plain_length, final_length;
    unsigned char *plaintext;

    plain_length = datax_length;
    plaintext = (unsigned char *)malloc(plain_length + 1);

    EVP_DecryptUpdate(ctx, plaintext, &plain_length, (unsigned char *)datax, datax_length);
    EVP_DecryptFinal_ex(ctx, plaintext + plain_length, &final_length);

    plaintext[plain_length + final_length] = '\0';

    free(datax);

    EVP_CIPHER_CTX_free(ctx);

    data = (const char*)plaintext;

    return data.size();
}

int main(int argc, const char** argv)
{
    string dsn;
    string id;
    string pw;
    string mode;
    int opt;

    if ( argc < 2 ) {
        fprintf(stderr, "CALLDBPROC - Usage calldbproc [min|day|encstr] [string]\n");
        return -2;
    }

    if (!strncmp(argv[1], "encstr", 6)) {
            if ( argc == 3 ) {
                string input = argv[2];
                if ( Encrypt(input) ) {
                    std::cout << "ENC STR(" << input << ")" << std::endl;
                }
            }
            return 0;
    }

    while( (opt = getopt(argc, (char *const *)argv, "1234")) != -1) {
        switch (opt) {
            case '1':
                mode = "min";
                break;
            case '2':
                mode = "sync";
                break;
            case '3':
                mode = "stat_job";
                break;
            case '4':
                mode = "stat_res";
                break;
            default:
                mode = "unknown";
        }
    }

    try {
        config = new Configuration(argc, argv);
    } catch (std::exception &e) {
        perror(e.what());
        return -1;
    }

    logger = config->getLogger();

    dsn = config->getConfig("database.dsn", "itfodbc");
    id = config->getConfig("database.id", "stt");
    if ( !config->getConfig("database.encrypt", "false").compare("true") )
    {
        pw = config->getConfig("database.pw", "144977AD622F41F741FF26D1CCF7E6DF");
        if ( !Decrypt(pw) )
            pw = config->getConfig("database.pw", "~dltvor2009");
    }
    else
    {
        pw = config->getConfig("database.pw", "~dltvor2009");
    }

    ODBC_Name = (SQLCHAR*)dsn.c_str();
    ODBC_ID = (SQLCHAR*)id.c_str();
    ODBC_PW = (SQLCHAR*)pw.c_str();

	if ( !DBConnect() ) {
        logger->error("CALLDBPROC - Failed to connect DB(%s)", dsn.c_str());
        return -1;
    }

    if ( !mode.compare("min") ) {
        if ( !DBExcuteSQL_MIN() )
            logger->error("CALLDBPROC - Failed to execute SQL_MIN");
    }
    else if ( !mode.compare("sync") ) {
        if ( !DBExcuteSQL_SYNC() )
            logger->error("CALLDBPROC - Failed to execute SQL_SYNC");
    }
    else if ( !mode.compare("stat_job") ) {
        if ( !DBExcuteSQL_STAT_JOB() )
            logger->error("CALLDBPROC - Failed to execute SQL_STAT_JOB");
    }
    else if ( !mode.compare("stat_res") ) {
        if ( !DBExcuteSQL_STAT_RES() )
            logger->error("CALLDBPROC - Failed to execute SQL_STAT_RES");
    }
    else {
        logger->warn("CALLDBPROC - Unknown mode(%s)", mode.c_str());
    }

	DBDistconnect();
	return 0;
}

BOOL DBConnect()
{
	if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv)) //이벤트 핸들 생성
		return FALSE;
	if (SQL_SUCCESS != SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0)) //핸들 환경설정
		return FALSE;
	if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc)) //접속 핸들 생성
		return FALSE;

	if(SQL_SUCCESS!=SQLConnect(hDbc, ODBC_Name, SQL_NTS, ODBC_ID, SQL_NTS, ODBC_PW, SQL_NTS))
		return FALSE;
	if(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt) != SQL_SUCCESS)
		return FALSE;
	
	return TRUE;
}

BOOL DBExcuteSQL_SYNC()
{	
	char squery[100];
	
	snprintf (squery, sizeof(squery), "PKG_STT.PROC_DLS_CHANNEL()");	
	if(SQLExecDirect(hStmt, (SQLCHAR *)squery, SQL_NTS) != SQL_SUCCESS)
	{
        return FALSE;
	}	
	return TRUE;	
}

BOOL DBExcuteSQL_STAT_JOB()
{	
	char squery[100];
	
	snprintf (squery, sizeof(squery), "PKG_STT.PROC_JOB_STAT_REG()");	
	if(SQLExecDirect(hStmt, (SQLCHAR *)squery, SQL_NTS) != SQL_SUCCESS)
	{
        return FALSE;
	}	
	return TRUE;	
}

BOOL DBExcuteSQL_STAT_RES()
{	
	char squery[100];
	
	snprintf (squery, sizeof(squery), "PKG_STT.PROC_RES_STAT_REG()");	
	if(SQLExecDirect(hStmt, (SQLCHAR *)squery, SQL_NTS) != SQL_SUCCESS)
	{
        return FALSE;
	}	
	return TRUE;	
}

BOOL DBExcuteSQL_MIN()
{	
	char squery[100];
	
	snprintf (squery, sizeof(squery), "PROC_CALL_MATCH()");	
	if(SQLExecDirect(hStmt, (SQLCHAR *)squery, SQL_NTS) != SQL_SUCCESS)
	{
		return FALSE;
	}	
	return TRUE;	
}

BOOL DBDistconnect()
{
	if (hStmt)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    }

    if (hDbc)
    {
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
    }

    if (hEnv)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
    }

    logger->info("CALLDBPROC - DISCONNECTED");

    return TRUE;
}
