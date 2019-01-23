
#include "ItfOdbcPool.h"
#include "stas.h"

#include <log4cpp/Category.hh>

#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <chrono>

ItfOdbcPool::ItfOdbcPool(const char* dsn, const char* id, const char* pw)
{
    //sprintf(m_sDsn, "DSN=%s;", dsn);
    sprintf(m_sDsn, "%s", dsn);
    sprintf(m_sId, "%s", id);
    sprintf(m_sPw, "%s", pw);
    m_nConnSetCount = 0;
    m_nNextId = 0;
}

ItfOdbcPool::~ItfOdbcPool()
{
    releaseConnections();
}

int ItfOdbcPool::createConnections(int setCount)
{
    PConnSet connSet = nullptr;
    SQLRETURN fsts;
    SQLHENV env;
    SQLHDBC dbc;
    SQLHSTMT stmt;
    SQLRETURN ret;
    log4cpp::Category *logger = config->getLogger();

    if (m_nConnSetCount > 0) return m_nConnSetCount;

    m_nConnSetCount = setCount;
    for(int i=0; i<setCount; i++) {
        /* Allocate an environment handle */
        fsts = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
        if (!SQL_SUCCEEDED(fsts))
        {
            /* an error occurred allocating the database handle */
            continue;
        }
        /* We want ODBC 3 support */
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);
        /* Allocate a connection handle */
        fsts = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
        if (!SQL_SUCCEEDED(fsts))
        {
            /* an error occurred allocating the database handle */
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            continue;
        }
        /* Connect to the DSN mydsn */
//         ret = SQLDriverConnect(dbc, NULL, (unsigned char *)m_sDsn, SQL_NTS,
//                                 NULL, 0, NULL,
//                                 SQL_DRIVER_COMPLETE);
#if 0
        ret = SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT,
                                        (SQLPOINTER)1, 0);
#endif
        // SQLSetConnectAttr(dbc, SQL_ATTR_CONNECTION_TIMEOUT, reinterpret_cast<SQLPOINTER>(0), SQL_IS_UINTEGER);
        ret = SQLConnect(dbc, (SQLCHAR*)m_sDsn, SQL_NTS,
                         (SQLCHAR*) m_sId, strlen(m_sId), (SQLCHAR*) m_sPw, strlen(m_sPw));
        if (SQL_SUCCEEDED(ret)) {
            /* Allocate a statement handle */
            fsts = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
            if (!SQL_SUCCEEDED(fsts))
            {
                /* an error occurred allocating the database handle */
                SQLDisconnect(dbc);
                SQLFreeHandle(SQL_HANDLE_DBC, dbc);
                SQLFreeHandle(SQL_HANDLE_ENV, env);
                continue;
            }

            connSet = new ConnSet();
            connSet->id = i;
            connSet->env = env;
            connSet->dbc = dbc;
            connSet->stmt = stmt;
            connSet->useStat = true;
            connSet->currStat = false;
            connSet->lastTime = time(NULL);

            m_mConnSets.insert(std::make_pair(i, connSet));
            // m_nConnSetCount++;
            logger->debug("ItfOdbcPool::createConnectinos - id(%d), m_nConnSetCount(%d)", i, m_nConnSetCount);
        }
        else {
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            continue;
        }
    }

    std::thread thrd = std::thread(ItfOdbcPool::updateConnection, this);
    thrd.detach();
    
    return m_nConnSetCount;
}

void ItfOdbcPool::releaseConnections()
{
    std::map<int, PConnSet>::iterator iter;

    for(iter=m_mConnSets.begin(); iter!=m_mConnSets.end(); iter++) {
        PConnSet connSet = iter->second;

        SQLFreeHandle(SQL_HANDLE_STMT, connSet->stmt);
        SQLDisconnect(connSet->dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, connSet->dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, connSet->env);

        delete connSet;
    }

    m_mConnSets.clear();
}

PConnSet    ItfOdbcPool::getConnection()   // race condition 방지를 위해 mutex를 이용해야 함.
{
    std::lock_guard<std::mutex> g(m_mxDb);
    std::map<int, PConnSet>::iterator iter;
    PConnSet connSet = nullptr;
    log4cpp::Category *logger = config->getLogger();

    for(iter=m_mConnSets.begin(); iter!=m_mConnSets.end(); iter++) {
        if ((iter->second)->useStat && !(iter->second)->currStat && ((iter->second)->id >= m_nNextId) ) {
            connSet = iter->second;
            connSet->currStat = true;
            m_nNextId = connSet->id + 1;
            if ( m_nNextId >= m_nConnSetCount ) m_nNextId = 0;
            break;
        }
    }
    
    logger->debug("ItfOdbcPool::getConnection - m_nNextId(%d), connSet(%d)", m_nNextId, connSet?1:0);
    return connSet;
}

void        ItfOdbcPool::restoreConnection(ConnSet *conn)  // 사용 완료된 커넥션은 반드시 반환해야 함.
{
    conn->currStat = false;
    conn->lastTime = time(NULL);
}

void        ItfOdbcPool::restoreConnectionNoSetTime(ConnSet *conn)  // 사용 완료된 커넥션은 반드시 반환해야 함.
{
    conn->currStat = false;
}

PConnSet    ItfOdbcPool::reconnectConnection(ConnSet *conn)    // 사용 중 문제가 생긴 커넥션에 대해 재연결
{
    //std::lock_guard<std::mutex> *g = nullptr;
    SQLRETURN fsts;
    SQLHENV env;
    SQLHDBC dbc;
    SQLHSTMT stmt;
    SQLRETURN ret;
    //int id = conn->id;
    //PConnSet connSet = m_mConnSets.find(id)->second;

    //g = new std::lock_guard<std::mutex>(m_mxDb);
    SQLFreeHandle(SQL_HANDLE_STMT, conn->stmt);
    SQLDisconnect(conn->dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, conn->dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, conn->env);
    //delete connSet;
    //m_mConnSets.erase(id);
    //m_nConnSetCount--;
    //delete g; g = nullptr;

    //connSet = nullptr;

    /* Allocate an environment handle */
    fsts = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (SQL_SUCCEEDED(fsts))
    {
        /* We want ODBC 3 support */
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);
        /* Allocate a connection handle */
        fsts = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
        if (SQL_SUCCEEDED(fsts))
        {
            /* Connect to the DSN mydsn */

//             ret = SQLDriverConnect(dbc, NULL, (unsigned char *)m_sDsn, SQL_NTS,
//                                     NULL, 0, NULL,
//                                     SQL_DRIVER_COMPLETE);

            ret = SQLConnect(dbc, (SQLCHAR*)m_sDsn, SQL_NTS,
                            (SQLCHAR*) m_sId, strlen(m_sId), (SQLCHAR*) m_sPw, strlen(m_sPw));

            if (SQL_SUCCEEDED(ret)) {
                /* Allocate a statement handle */
                fsts = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
                if (SQL_SUCCEEDED(fsts))
                {
                    conn->env = env;
                    conn->dbc = dbc;
                    conn->stmt = stmt;
                    conn->lastTime = time(NULL);
                    return conn;
                }

                /* an error occurred allocating the database handle */
                SQLDisconnect(dbc);
                SQLFreeHandle(SQL_HANDLE_DBC, dbc);
                SQLFreeHandle(SQL_HANDLE_ENV, env);

                //connSet = new ConnSet();
                //connSet->id = id;
                // conn->env = env;
                // conn->dbc = dbc;
                // conn->stmt = stmt;
                // conn->useStat = true;
                // conn->currStat = false;

                // g = new std::lock_guard<std::mutex>(m_mxDb);
                // m_mConnSets.insert(std::make_pair(id, connSet));
                // m_nConnSetCount++;
                // delete g; g = nullptr;
            }
            else {
                SQLFreeHandle(SQL_HANDLE_DBC, dbc);
                SQLFreeHandle(SQL_HANDLE_ENV, env);
            }
        }
        else {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
        }
    }

    return nullptr;
}

void ItfOdbcPool::eraseConnection(ConnSet *conn)
{
    std::map<int, PConnSet>::iterator iter;

    for(iter=m_mConnSets.begin(); iter!=m_mConnSets.end(); iter++) {
        PConnSet connSet = iter->second;

        if ( connSet->id == conn->id )
        {

            SQLFreeHandle(SQL_HANDLE_STMT, conn->stmt);
            SQLDisconnect(conn->dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, conn->dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, conn->env);

            delete conn;
            m_mConnSets.erase(iter);
            break;
        }
    }

    m_mConnSets.clear();
}

void ItfOdbcPool::updateConnection(ItfOdbcPool *pool)
{
    PConnSet connSet = nullptr;
    char sqlbuff[512];
    SQLRETURN retcode;
    RETCODE rc = SQL_SUCCESS;
    time_t currT = time(NULL);
    // char callid[33];
    log4cpp::Category *logger = config->getLogger();

    while(1) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        connSet = pool->getConnection();

        if (connSet)
        {
            currT = time(NULL);
            // logger->debug("ItfOdbcPool::updateConnection - currT(%ld), connSet_lastTime(%ld)", currT, connSet->lastTime);
            // if ( (currT - connSet->lastTime) < 100 )
            // {
            //     pool->restoreConnectionNoSetTime(connSet);
            //     continue;
            // }
    #if defined(USE_ORACLE) || defined(USE_TIBERO)
            sprintf(sqlbuff, "SELECT 1 FROM DUAL");
    #else
            sprintf(sqlbuff, "SELECT 1");
    #endif

            retcode = SQLExecDirect(connSet->stmt, (SQLCHAR *)sqlbuff, SQL_NTS);

            if SQL_SUCCEEDED(retcode) {
                while (1)//(SQLFetch(connSet->stmt) == SQL_SUCCESS) 
                {
                    rc = SQLFetch( connSet->stmt );
                    break;
                }
            }

            logger->debug("ItfOdbcPool::updateConnection - timeoffset(%ld), UPDATED CONNSET(%d)", (currT - connSet->lastTime), connSet->id);

            retcode = SQLCloseCursor(connSet->stmt);
            pool->restoreConnection(connSet);
        }
    }
}
