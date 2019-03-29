#include "Utils.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <regex>
#include <algorithm>
#include <locale>

#include <boost/locale/encoding_utf.hpp>
#include <boost/algorithm/string.hpp>


#include <fvad.h>
#include <openssl/evp.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <string>
#include <map>
#include <vector>

using namespace std;
using boost::locale::conv::utf_to_utf;

map< string, string > g_mReplace;

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

#if 0
wstring s2ws(const std::string& str)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.from_bytes(str);
}

string ws2s(const std::wstring& wstr)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.to_bytes(wstr);
}
#else
std::wstring s2ws(const std::string& str)
{
    return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string ws2s(const std::wstring& str)
{
    return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
}  
#endif

void MakeDirectory(const char *full_path)
{
    char temp[256], *sp;

    strcpy(temp, full_path); // 경로문자열을 복사
    sp = temp; // 포인터를 문자열 처음으로

    while((sp = strchr(sp, '/'))) { // 디렉토리 구분자를 찾았으면
        if(sp > temp && *(sp - 1) != ':') { // 루트디렉토리가 아니면
        *sp = '\0'; // 잠시 문자열 끝으로 설정
        mkdir(temp, S_IFDIR | S_IRWXU | S_IRWXG | S_IXOTH | S_IROTH);
        // 디렉토리를 만들고 (존재하지 않을 때)
        *sp = '/'; // 문자열을 원래대로 복귀
    }
        sp++; // 포인터를 다음 문자로 이동
    }
}

void remSpaceInSentence(string& org)
{
    wstring pattern = L"[일이삼사오육륙칠팔구십백만천억\\s]{5,}";
    wregex we(pattern);
    wsmatch wm;
    wstring convWStr;
    wstring tempWStr;
    wstring mResult;
    size_t spos, slen, epos;

    convWStr = s2ws( org );
    tempWStr = convWStr;

    spos = slen = epos = 0;
    while( regex_search( tempWStr, wm, we) )
    {
        mResult = wm.str(0);
        spos = tempWStr.find( mResult );

        if ( !spos || ( tempWStr[spos] == L' ' ) ) {

            slen = mResult.size();

            mResult.erase(remove(mResult.begin(), mResult.end(), L' '), mResult.end());
            mResult.insert(0, L" ");

            convWStr.replace(spos + epos, slen, mResult);

            epos = epos + spos + mResult.size();

            tempWStr = convWStr.substr(epos);
}
        else {
            tempWStr = tempWStr.substr(spos + 1);
            epos = epos + spos + 1;
        }
    }

    org = ws2s( convWStr ) ;

}


void maskKeyword(string& org)
{
    wstring pattern = L"[공영일이삼사오육륙칠팔구\\s]{5,}";
    wregex we(pattern);
    wsmatch wm;
    wstring convWStr;
    wstring tempWStr;
    wstring mResult;
    wstring maskValue = L" *** ";
    size_t spos, slen, epos;

    convWStr = s2ws( org );
    tempWStr = convWStr;

    spos = slen = epos = 0;
    while( regex_search( tempWStr, wm, we) )
    {
        mResult = wm.str(0);
        spos = tempWStr.find( mResult );

        if ( !spos || ( tempWStr[spos] == L' ' ) ) {

            slen = mResult.size();

            convWStr.replace(spos + epos, slen, maskValue);

            epos = epos + spos + maskValue.size();

            tempWStr = convWStr.substr(epos);
        }
        else
        {
            tempWStr = tempWStr.substr(spos + 1);
            epos = epos + spos + 1;
        }
    }

    org = ws2s( convWStr ) ;

}

int checkRealSize(std::vector<uint8_t> &buff, uint16_t hSize, size_t fSize, size_t vadFSize)
{
    Fvad *vad = NULL;
    int realSize=0;
    uint32_t lenVoiceData = buff.size() - hSize;
    uint8_t *voiceData = (uint8_t*)&buff[hSize];
    uint8_t *vpBuf = nullptr;
    int vadres = 0;

    vad = fvad_new();
    if (!vad) {//} || (fvad_set_sample_rate(vad, in_info.samplerate) < 0)) {
        // client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
        return buff.size();
    }
    fvad_set_sample_rate(vad, 8000);
    fvad_set_mode(vad, 3);

    size_t posBuf = 0;
    while ((lenVoiceData >= fSize) && ((lenVoiceData - posBuf) >= fSize)) {
        vpBuf = (uint8_t *)(voiceData+posBuf);
        // Convert the read samples to int16
        vadres = fvad_process(vad, (const int16_t *)vpBuf, vadFSize);

        if (vadres > 0) realSize += fSize;

        posBuf += fSize;
    }

    fvad_free(vad);

    return realSize;

}

int createReplaceMap(const char* mapFile)
{
    std::ifstream infile(mapFile);
    std::string line;
    std::vector<std::string> strs;

    if (infile.fail()) {
        std::cerr << "Error opeing a file : " << mapFile << std::endl;
        infile.close();
        return -1;
    }
    while (std::getline(infile, line))
    {
        boost::split(strs, line, boost::is_any_of(","));

        if ( strs.size() < 2 ) continue;

        g_mReplace.insert(make_pair(strs[0], strs[1]));

    }
    infile.close();

    return 0;
}

std::string ReplaceAll(std::string &str, const std::string& from, const std::string& to){
    size_t start_pos = 0; //string처음부터 검사
    while((start_pos = str.find(from, start_pos)) != std::string::npos)  //from을 찾을 수 없을 때까>지
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // 중복검사를 피하고 from.length() > to.length()인 경우를 위해서
    }
    return str;
}

void replaceSentence(string& sttValue)
{
    for (auto it = g_mReplace.begin(); it != g_mReplace.end(); it++ )
    {
        ReplaceAll(sttValue, it->first, it->second);
    }
}

