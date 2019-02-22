#ifndef DBHandler_H
#define DBHandler_H

#include <stdint.h>

#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <list>

#include <log4cpp/Category.hh>

#ifdef USE_FIND_KEYWORD
#include <list>
#endif

#ifdef USE_REDIS_POOL
#include "RedisHandler.h"

using namespace xrc;
#endif // USE_REDIS_POOL

class ItfOdbcPool;

class JobInfoItem {
    public:
    std::string m_callid;
    std::string m_counselorcode;
    std::string m_path;
    std::string m_filename;
    std::string m_regdate;
    std::string m_rxtx;
    std::string m_tableName; // STT_TBL_JOB_INFO, STT_TBL_JOB_SELF_INFO, STT_TBL_JOB_RETRY_INFO
    int m_procNo;

    public:
    JobInfoItem(std::string callid, std::string counselorcode, std::string path, std::string filename, std::string regdate, std::string rxtx, std::string tableName="STT_TBL_JOB_INFO", int procNo=0);
    virtual ~JobInfoItem();

    std::string getCallId() { return m_callid; }
    std::string getCounselorCode() { return m_counselorcode; }
    std::string getPath() { return m_path; }
    std::string getFilename() {return m_filename;}
    std::string getRegdate() {return m_regdate;}
    std::string getRxTxType() { return m_rxtx; }

    void setTableName(std::string tbName) { m_tableName = tbName; }
    std::string getTableName() { return m_tableName; }
};

#ifdef USE_UPDATE_POOL
class UpdateInfoItem {
public:
    std::string callid;
    std::string m_regdate;
    std::string rxtx;
    std::string counselorcode;
    char state;
    int fsize;
    int plen;
    int wtime;
    int procNo;
    std::string tbName;
    std::string errcode;
    std::string svr_nm;

public:
    UpdateInfoItem(std::string callid, std::string regdate, std::string rxtx, std::string counselorcode, char state, int fsize, int plen, int wtime, int procNo, const char *tbName, const char *errcode, const char *svr_nm);
    virtual ~UpdateInfoItem();

    std::string getCallId() { return callid; }
    std::string getRxTx() { return rxtx; }
    std::string getCounselorCode() { return counselorcode; }
    char getState() { return state; }
    int getFileSize() { return fsize; }
    int getPlayLength() { return plen; }
    int getWorkingTime() { return wtime; }
    std::string getTableName() { return tbName;}
    std::string getErrCode() { return errcode ;}
    std::string getServerName() { return svr_nm; }

};
#endif

class RTSTTQueItem {
    uint32_t m_nDiaIdx;
	std::string m_sCallId;
	uint8_t m_nSpkNo;		// 1 ~ 9 : 통화자 구분값, 0 : 작업타입이 File인 경우
	std::string m_sSTTValue;

	uint64_t m_nBpos;
	uint64_t m_nEpos;

public:
	RTSTTQueItem(uint32_t idx, std::string callid, uint8_t spkno, std::string &sttvalue, uint64_t bpos, uint64_t epos);
	virtual ~RTSTTQueItem();

	uint32_t getDiaIdx() { return m_nDiaIdx; }
	std::string& getCallId() { return m_sCallId; }
	uint8_t getSpkNo() { return m_nSpkNo; }
	std::string& getSTTValue() { return m_sSTTValue; }
    uint64_t getBpos() { return m_nBpos; }
    uint64_t getEpos() { return m_nEpos; }
};

class DBHandler {

public:
    static DBHandler* instance(std::string dsn, std::string id, std::string pw, int connCount);
    static void release();
    static DBHandler* getInstance();

    virtual ~DBHandler();

    void setDsn(const std::string dsn);
    void setConnectionCount(int cnt);

    // for Realtime Call Siganl
    // VRClient에서 사용되는 api이며 실시간 통화 시작 및 종료 시 사용된다.
    int searchCallInfo(std::string counselorcode);
    int insertCallInfo(std::string counselorcode, std::string callid);
    int updateCallInfo(std::string callid, bool end=false);
    int updateCallInfo(std::string counselorcode, std::string callid, bool end=false);
    void insertSTTData(uint32_t idx, std::string callid, uint8_t spkno, uint64_t spos, uint64_t epos, std::string &stt);
    
    int insertFullSttData(std::string callid, std::string &stt);
    // for batch
    // Scheduler 모듈에서 사용되는 api
    // 처리할 task가 등록되었는지 확인(search)하고
    // 신규 task에 대해 VFClient가 처리할 수 있도록 전달(get) 후 처리된 task에 대해서는 삭제(delete)한다.
    void insertBatchTask();
    int getBatchTask();
    void deleteBatchTask();
    void updateAllTask2Fail();  // 일정 시간(1시간)이 경과된 작업 중인 task에 대해 '실패' 상태로 변경
    void updateAllTask2Fail2();
    void updateAllIncompleteTask2Fail();

    // for Task working
    // VFClient에서 사용되는 api로서 작업 시작 전,
    // 작업 완료 후 아래의 api를 이용하여 해당 task에 대한 정보를 handling한다.
    int insertTaskInfo(std::string downloadPath, std::string filename, std::string callId);
    int updateTaskInfo(std::string callid, std::string regdate, std::string rxtx, std::string counselorcode, char state, int fsize=0, int plen=0, int wtime=0, int procNo=0, const char *tbName="STT_TBL_JOB_INFO", const char *errcode=nullptr, const char *svr_nm="DEFAULT");
    int updateTaskInfo(std::string callid, std::string rxtx, std::string counselorcode, std::string regdate, char state, int fsize=0, int plen=0, int wtime=0, const char *tbName="STT_TBL_JOB_INFO", const char *errcode=nullptr, const char *svr_nm="DEFAULT");
    int searchTaskInfo(std::string downloadPath, std::string filename, std::string callId);
    int getTaskInfo(std::vector< JobInfoItem* > &v, int count, const char *tableName="STT_TBL_JOB_INFO");
    int getTaskInfo2(std::vector< JobInfoItem* > &v, int count, const char *tableName="STT_TBL_JOB_SELF_INFO");
    int getTimeoutTaskInfo(std::vector< JobInfoItem* > &v);
    int insertTaskInfoRT(std::string downloadPath, std::string filename, std::string callId, std::string counselcode, time_t startT);
    int updateTaskInfo4Schd(std::string callid, std::string regdate, std::string rxtx, std::string tbName);
    int getIncompleteTask(std::vector< JobInfoItem* > &v);
    int getIncompleteTaskFromSelf(std::vector< JobInfoItem* > &v);
    int getIncompleteTaskFromRetry(std::vector< JobInfoItem* > &v);

    // void restartConnectionPool();

    // API for Interface DB
    void setInterDBEnable(std::string dsn, std::string id, std::string pw, int conCount);
    void setInterDBDisable();

    int deleteJobData(std::string counselorcode);
    int deleteJobInfo(std::string counselorcode);

private:
    DBHandler(std::string dsn, int connCount);
	static void thrdMain(DBHandler* s2d);
	void insertSTTData(RTSTTQueItem* item);
#ifdef USE_UPDATE_POOL
    static void thrdUpdate(DBHandler* handle);
#endif

#ifdef USE_FIND_KEYWORD
    static void thrdUpdateKeywords(DBHandler* handle);
public:
    static std::list< std::string > getKeywords();
#endif

private:
    std::string m_sDsn;
    int m_nConnCount;

	bool m_bLiveFlag;
    bool m_bInterDBUse;

	std::queue< RTSTTQueItem* > m_qRtSttQue;
	std::thread m_thrd;
#ifdef USE_UPDATE_POOL
    std::queue< UpdateInfoItem* > m_qUpdateInfoQue;
    std::thread m_thrdUpdate;
    mutable std::mutex m_mxUpdateQue;
#endif

#ifdef USE_FIND_KEYWORD
    static std::list< std::string > m_lKeywords;
    std::thread m_thrdUpdateKeywords;
#endif

	mutable std::mutex m_mxQue;
	mutable std::mutex m_mxDb;
    log4cpp::Category *m_Logger;

    ItfOdbcPool *m_pSolDBConnPool;
    ItfOdbcPool *m_pInterDBConnPool;

    static DBHandler* m_instance;

    static bool m_bThrdMain;
#ifdef USE_UPDATE_POOL
    static bool m_bThrdUpdate;
#endif

#ifdef USE_FIND_KEYWORD
    static bool m_bThrdUpdateKeywords;
    static uint8_t m_bUpdateKeywordFlag;
#endif

// #ifdef USE_REDIS_POOL
//     xRedisClient &s_xRedis;
//     // RedisDBIdx s_dbi;
// #endif

    bool m_bUseMask;

    std::string m_sNotiChannel;
    bool m_bSaveStt;

    bool m_buseRedis;
    bool m_buseRedisPool;

    bool m_bUseRemSpaceInNumwords;

};


#endif // DBHandler_H
