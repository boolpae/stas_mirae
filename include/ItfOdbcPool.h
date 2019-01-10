#ifndef ITF_ODBC_POOL
#define ITF_ODBC_POOL

#include <sql.h>
#include <sqlext.h>

#include <map>
#include <mutex>

typedef struct _ConnSet {
    unsigned int id;
    SQLHENV env;
    SQLHDBC dbc;
    SQLHSTMT stmt;
    bool useStat;   // 사용이 가능한지 아닌지를 나타내는 Flag: true - 사용가능, false - 사용불가
    bool currStat;  // 현재 사용 중인지 아닌지를 나타내는 flag: true - 현재 사용 중, false - 현재 대기 중
} ConnSet, *PConnSet;

class ItfOdbcPool {

public:
    ItfOdbcPool(const char* dsn, const char* id, const char* pw);
    virtual ~ItfOdbcPool();

    int createConnections(int setCount);
    void releaseConnections();

    PConnSet    getConnection();   // race condition 방지를 위해 mutex를 이용해야 함.
    void        restoreConnection(ConnSet *conn);  // 사용 완료된 커넥션은 반드시 반환해야 함.
    PConnSet    reconnectConnection(ConnSet *conn);    // 사용 중 문제가 생긴 커넥션에 대해 재연결
    int         getConnSetCount() { return m_nConnSetCount; }
    
private:
    mutable std::mutex m_mxDb;
    std::map<int, PConnSet> m_mConnSets;
    int  m_nConnSetCount;
    char m_sDsn[128];
    char m_sId[64];
    char m_sPw[64];

};

#endif
