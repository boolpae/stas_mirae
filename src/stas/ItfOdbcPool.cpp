
#include "ItfOdbcPool.h"

#include <stdio.h>
#include <string.h>
#include <string>

ItfOdbcPool::ItfOdbcPool(const char* dsn, const char* id, const char* pw)
{
    //sprintf(m_sDsn, "DSN=%s;", dsn);
    sprintf(m_sDsn, "%s", dsn);
    sprintf(m_sId, "%s", id);
    sprintf(m_sPw, "%s", pw);
    m_nConnSetCount = 0;
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

    if (m_nConnSetCount > 0) return m_nConnSetCount;

    m_nConnSetCount = 0;
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

            m_mConnSets.insert(std::make_pair(i, connSet));
            m_nConnSetCount++;
        }
        else {
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            continue;
        }
    }
    
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

    for(iter=m_mConnSets.begin(); iter!=m_mConnSets.end(); iter++) {
        if ((iter->second)->useStat && !(iter->second)->currStat) {
            connSet = iter->second;
            connSet->currStat = true;
            break;
        }
    }
    
    return connSet;
}

void        ItfOdbcPool::restoreConnection(ConnSet *conn)  // 사용 완료된 커넥션은 반드시 반환해야 함.
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

