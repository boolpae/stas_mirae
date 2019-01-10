
#include "DBHandler.h"
#include "stas.h"

#include <iconv.h>

#include <string.h>

DBHandler* DBHandler::m_instance = nullptr;


DBHandler::DBHandler(/*log4cpp::Category *logger*/)
: m_bLiveFlag(true), /*m_Logger(logger),*/ m_bInterDBUse(false)
{
    m_Logger = config->getLogger();
	m_Logger->debug("DBHandler Constructed.");
}

DBHandler::~DBHandler()
{
    m_instance->m_bLiveFlag = false;

    ConnectionPool_stop(m_pool);
    ConnectionPool_free(&m_pool);
    URL_free(&m_url);
    if (m_thrd.joinable()) m_thrd.detach();

    if (m_bInterDBUse) {
        ConnectionPool_stop(m_ExPool);
        ConnectionPool_free(&m_ExPool);
        URL_free(&m_ExUrl);
    }
        
	m_Logger->debug("DBHandler Destructed.");
}

void DBHandler::thrdMain(DBHandler * s2d)
{
	std::lock_guard<std::mutex> *g;// (m_mxQue);
	RTSTTQueItem* item;

    int ret=0;
    char *utf_buf = NULL;
    size_t in_size, out_size;
    iconv_t it;
    char *input_buf_ptr = NULL;
    char *output_buf_ptr = NULL;

    Connection_T con;
    con = ConnectionPool_getConnection(s2d->m_pool);
    

	while (s2d->m_bLiveFlag) {
		while (!s2d->m_qRtSttQue.empty()) {
			g = new std::lock_guard<std::mutex>(s2d->m_mxQue);
			item = s2d->m_qRtSttQue.front();
			s2d->m_qRtSttQue.pop();
			delete g;

            in_size = item->getSTTValue().size();
            out_size = item->getSTTValue().size() * 2;
            utf_buf = (char *)malloc(out_size);
            
            if (utf_buf) {
                memset(utf_buf, 0, out_size);

                input_buf_ptr = (char *)item->getSTTValue().c_str();
                output_buf_ptr = utf_buf;

                it = iconv_open("UTF-8", "EUC-KR");

                ret = iconv(it, &input_buf_ptr, &in_size, &output_buf_ptr, &out_size);
                 
                iconv_close(it);
                
            }
            else {
                utf_buf = nullptr;
                ret = -1;
            }
            
            // insert rtstt to db
            TRY
            {
                char cSpk='N';

                switch(item->getSpkNo()) {
                    case 1:
                        cSpk = 'R';
                        break;
                    case 2:
                        cSpk = 'L';
                        break;
                    default:
                        cSpk = 'N';
                }

                Connection_execute(con, "INSERT INTO TBL_JOB_DATA (IDX,CALL_ID,SPK,POS_START,POS_END,VALUE) VALUES (%d,'%s','%c',%lu,%lu,'%s')",
                item->getDiaIdx(), item->getCallId().c_str(), cSpk, item->getBpos(), item->getEpos(), ((ret == -1) ? item->getSTTValue().c_str() : utf_buf));
            }
            CATCH(SQLException)
            {
                s2d->m_Logger->error("DBHandler::insertCallInfo - SQLException -- %s", Exception_frame.message);
                Connection_close(con);
                con = ConnectionPool_getConnection(s2d->m_pool);
            }
            END_TRY;

            if (utf_buf) free(utf_buf);
			delete item;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
    if (DBHandler::getInstance())
        Connection_close(con);
}

DBHandler* DBHandler::instance(std::string dbtype, std::string dbhost, std::string dbport, std::string dbuser, std::string dbpw, std::string dbname, std::string charset/*, log4cpp::Category *logger*/)
{
    if (m_instance) return m_instance;
    
    m_instance = new DBHandler(/*logger*/);
    
    std::string sUrl = dbtype + "://" + dbuser + ":" + dbpw + "@" + dbhost + ":" + dbport + "/" + dbname + "?charset=" + charset;
    m_instance->m_url = URL_new(sUrl.c_str());
    m_instance->m_pool = ConnectionPool_new(m_instance->m_url);
    
    TRY
    {
        ConnectionPool_start(m_instance->m_pool);
        //ConnectionPool_setReaper(m_instance->m_pool, 10);
    }
    CATCH(SQLException)
    {
        log4cpp::Category *logger = config->getLogger();
        logger->error("DBHandler::instance - SQLException -- %s", Exception_frame.message);
        
        ConnectionPool_free(&m_instance->m_pool);
        URL_free(&m_instance->m_url);
        
        delete m_instance;
        m_instance = nullptr;
    }
    END_TRY;
    
    m_instance->m_thrd = std::thread(DBHandler::thrdMain, m_instance);

    return m_instance;
}

void DBHandler::release()
{
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

DBHandler* DBHandler::getInstance()
{
    if (m_instance) {
        return m_instance;
    }
    return nullptr;
}

int DBHandler::searchCallInfo(std::string counselorcode)
{
    Connection_T con;
    int ret=0;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::searchCallInfo - can't get connection from pool");
            restartConnectionPool();
            return -1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::searchCallInfo - inactive connection from pool");
            Connection_close(con);
            return -2;
        }
        ResultSet_T r = Connection_executeQuery(con, "SELECT CS_CD,CT_CD,STAT FROM TBL_CS_LIST WHERE CS_CD='%s'", counselorcode.c_str());

        ret = ResultSet_next(r);
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::searchCallInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return ret;
}

int DBHandler::insertCallInfo(std::string counselorcode, std::string callid)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::insertCallInfo - can't get connection from pool");
            restartConnectionPool();
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::insertCallInfo - inactive connection from pool");
            Connection_close(con);
            return 2;
        }
        Connection_execute(con, "INSERT INTO TBL_CS_LIST (CS_CD,CALL_ID,CT_CD,STAT,REG_DTM) VALUES ('%s','%s','1','I',now())",
        counselorcode.c_str(), callid.c_str());
        m_Logger->debug("DBHandler::insertCallInfo - SQL[INSERT INTO TBL_CS_LIST (CS_CD,CALL_ID,CT_CD,STAT,REG_DTM) VALUES ('%s','%s','1','I',now())]", counselorcode.c_str(), callid.c_str());
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::insertCallInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return 0;
}

int DBHandler::updateCallInfo(std::string callid, bool end)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::updateCallInfo - can't get connection from pool");
            restartConnectionPool();
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::updateCallInfo - inactive connection from pool");
            Connection_close(con);
            return 2;
        }
        if (!end) {
            Connection_execute(con, "UPDATE TBL_CS_LIST SET STAT='I' WHERE CALL_ID='%s'",
            callid.c_str());
        }
        else {
            Connection_execute(con, "UPDATE TBL_CS_LIST SET STAT='E' WHERE CALL_ID='%s'",
            callid.c_str());
        }
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::updateCallInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return 0;
}

int DBHandler::updateCallInfo(std::string counselorcode, std::string callid, bool end)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::updateCallInfo - can't get connection from pool");
            restartConnectionPool();
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::updateCallInfo - inactive connection from pool");
            Connection_close(con);
            return 2;
        }
        if (!end) {
            Connection_execute(con, "UPDATE TBL_CS_LIST SET STAT='I', CALL_ID='%s' WHERE CS_CD='%s'",
            callid.c_str(), counselorcode.c_str());
            m_Logger->debug("DBHandler::updateCallInfo - SQL[UPDATE TBL_CS_LIST SET STAT='I', CALL_ID='%s' WHERE CS_CD='%s']",callid.c_str(), counselorcode.c_str());
        }
        else {
            Connection_execute(con, "UPDATE TBL_CS_LIST SET STAT='E', CALL_ID='%s' WHERE CS_CD='%s'",
            callid.c_str(), counselorcode.c_str());
            m_Logger->debug("DBHandler::updateCallInfo - SQL[UPDATE TBL_CS_LIST SET STAT='E', CALL_ID='%s' WHERE CS_CD='%s']",callid.c_str(), counselorcode.c_str());
        }
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::updateCallInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return 0;
}
#if 0
int DBHandler::insertSTTData(uint32_t idx, std::string callid, uint8_t spkno, uint64_t spos, uint64_t epos, std::string stt)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            printf("DBHandler::insertSTTData - can't get connection from pool\n");
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            printf("DBHandler::insertSTTData - inactive connection from pool\n");
            Connection_close(con);
            return 2;
        }
        Connection_execute(con, "INSERT INTO RTSTT_DATA (IDX,CALL_ID,SPKNO,SPOS,EPOS,STT,REGTIME) VALUES (%d,'%s','%c',%lu,%lu,'%s',now())",
        idx, callid.c_str(),((spkno == 1)?'R':'L'), spos/160, epos/160, stt.c_str());
    }
    CATCH(SQLException)
    {
        printf("DBHandler::insertSTTData - SQLException -- %s\n", Exception_frame.message);
    }
    FINALLY
    {
        Connection_close(con);
    }
    END_TRY;
    return 0;
}
#endif
void DBHandler::insertSTTData(uint32_t idx, std::string callid, uint8_t spkno, uint64_t spos, uint64_t epos, std::string stt)
{
	insertSTTData(new RTSTTQueItem(idx, callid, spkno, stt, spos, epos));
}

void DBHandler::insertSTTData(RTSTTQueItem * item)
{
	std::lock_guard<std::mutex> g(m_mxQue);
	m_qRtSttQue.push(item);
}

void DBHandler::insertBatchTask()
{
    // insert
}

int DBHandler::getBatchTask()
{
	std::lock_guard<std::mutex> g(m_mxDb);
    
    // select, update
    
    return 0;
}

void DBHandler::deleteBatchTask()
{
    // delete
}

// VFCLient모듈에서 사용되는 api로서 해당 task를 작업하기 직전 DB에 task 정보를 등록할 때 사용
// args: call_id, counselor_code etc
int DBHandler::insertTaskInfo(std::string downloadPath, std::string filename, std::string callId)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::insertTaskInfo - can't get connection from pool");
            restartConnectionPool();
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::insertTaskInfo - inactive connection from pool");
            Connection_close(con);
            return 2;
        }
        Connection_execute(con, "INSERT INTO TBL_JOB_INFO (CALL_ID,PATHNAME,FILE_NAME,REG_DTM,STATE) VALUES ('%s','%s','%s',now(),'N')",
        callId.c_str(), downloadPath.c_str(), filename.c_str());
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::insertTaskInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return 0;
}

// VFClient모듈에서 사용되는 api로서 해당 task 작업 종료 후 상태 값을 update할 때 사용
// args: call_id, counselor_code, task_stat etc
int DBHandler::updateTaskInfo(std::string callid, std::string counselorcode, char state)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::updateTaskInfo - can't get connection from pool");
            restartConnectionPool();
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::updateTaskInfo - inactive connection from pool");
            Connection_close(con);
            return 2;
        }
        Connection_execute(con, "UPDATE TBL_JOB_INFO SET STATE='%c' WHERE CALL_ID='%s' and CS_CODE='%s'",
        state, callid.c_str(), counselorcode.c_str());
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::updateTaskInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return 0;
}

int DBHandler::updateTaskInfo(std::string callid, std::string counselorcode, std::string regdate, char state)
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::updateTaskInfo - can't get connection from pool");
            restartConnectionPool();
            return 1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::updateTaskInfo - inactive connection from pool");
            Connection_close(con);
            return 2;
        }
        Connection_execute(con, "UPDATE TBL_JOB_INFO SET STATE='%c' WHERE CALL_ID='%s' AND CS_CODE='%s' AND REG_DTM='%s'",
        state, callid.c_str(), counselorcode.c_str(), regdate.c_str());
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::updateTaskInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return 0;
}

// VFClient모듈에서 사용되는 api로서 해당 task에 대해 이전에 작업한 내용인지 아닌지 확인하기 위해 사용
// args: call_id, counselor_code etc
int DBHandler::searchTaskInfo(std::string downloadPath, std::string filename, std::string callId)
{
    Connection_T con;
    int ret=0;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::searchTaskInfo - can't get connection from pool");
            restartConnectionPool();
            return -1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::searchTaskInfo - inactive connection from pool");
            Connection_close(con);
            return -2;
        }
        ResultSet_T r = Connection_executeQuery(con, "SELECT CALL_ID,CS_CODE,PATH,FILE_NAME FROM TBL_JOB_INFO WHERE CALL_ID='%s' AND FILE_NAME='%s'",
        callId.c_str(), filename.c_str());

        ret = ResultSet_next(r);
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::searchTaskInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;
    return ret;
}

int DBHandler::getTaskInfo(std::vector< JobInfoItem* > &v, int availableCount) 
{
    Connection_T con;
    int ret=0;

    m_Logger->debug("BEFORE DBHandler::getTaskInfo - ConnectionPool_size(%d), ConnectionPool_active(%d)", ConnectionPool_size(m_pool), ConnectionPool_active(m_pool));
    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::getTaskInfo - can't get connection from pool");
            restartConnectionPool();
            return -1;
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::getTaskInfo - inactive connection from pool");
            Connection_close(con);
            return -2;
        }
        ResultSet_T r = Connection_executeQuery(con, "SELECT CALL_ID,CS_CODE,PATHNAME,FILE_NAME,REG_DTM FROM TBL_JOB_INFO WHERE STATE='N' ORDER BY REG_DTM asc LIMIT %d", availableCount);

        while (ResultSet_next(r)) 
        {
            std::string callid = ResultSet_getString(r, 1);
            std::string counselorcode = ResultSet_getString(r, 2);
            std::string path = ResultSet_getString(r, 3);
            std::string filename = ResultSet_getString(r, 4);
            std::string regdate = ResultSet_getString(r, 5);
            bool rxtx = false;
            //rxtx = ResultSet_getInt(r, 5);

            JobInfoItem *item = new JobInfoItem(callid, counselorcode, path, filename, regdate, rxtx);
            v.push_back(item);
        }

        ret = v.size();
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::getTaskInfo - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        // Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
        m_Logger->debug("AFTER DBHandler::getTaskInfo - ConnectionPool_size(%d), ConnectionPool_active(%d)", ConnectionPool_size(m_pool), ConnectionPool_active(m_pool));
    }
    END_TRY;
    return ret;
}

void DBHandler::restartConnectionPool()
{
    m_Logger->debug("DBHandler::restartConnectionPool - size(%d), active(%d)", ConnectionPool_size(m_pool), ConnectionPool_active(m_pool));
    ConnectionPool_stop(m_pool);
    ConnectionPool_start(m_pool);
}

void DBHandler::updateAllTask2Fail()
{
    Connection_T con;

    TRY
    {
        con = ConnectionPool_getConnection(m_pool);
        if ( con == NULL) {
            m_Logger->error("DBHandler::updateAllTask2Fail - can't get connection from pool");
            restartConnectionPool();
        }
        else if ( !Connection_ping(con) ) {
            m_Logger->error("DBHandler::updateAllTask2Fail - inactive connection from pool");
            Connection_close(con);
        }
        Connection_execute(con, "UPDATE TBL_JOB_INFO SET STATE='X' WHERE RG_DTM >= concat(date(now()), ' 00:00:00') and RG_DTM <= concat(date(now()), '23:59:59') and STATE='U' and timestampdiff(minute, concat(date(now()), ' 00:00:00'), RG_DTM) > 59");
    }
    CATCH(SQLException)
    {
        m_Logger->error("DBHandler::updateAllTask2Fail - SQLException -- %s", Exception_frame.message);
    }
    FINALLY
    {
        //Connection_close(con);
        ConnectionPool_returnConnection(m_pool, con);
    }
    END_TRY;

}

void DBHandler::setInterDBEnable(std::string dbtype, std::string dbhost, std::string dbport, std::string dbuser, std::string dbpw, std::string dbname, std::string charset)
{
    std::string sUrl = dbtype + "://" + dbuser + ":" + dbpw + "@" + dbhost + ":" + dbport + "/" + dbname + "?charset=" + charset;
    
    if (m_bInterDBUse) return;
    
    m_ExUrl = URL_new(sUrl.c_str());
    m_ExPool = ConnectionPool_new(m_ExUrl);
    
    TRY
    {
        ConnectionPool_start(m_ExPool);
        //ConnectionPool_setReaper(m_instance->m_pool, 10);
        m_bInterDBUse = true;
    }
    CATCH(SQLException)
    {
        //logger->error("DBHandler::instance - SQLException -- %s", Exception_frame.message);
        
        ConnectionPool_free(&m_ExPool);
        URL_free(&m_ExUrl);
        
    }
    END_TRY;

}

void DBHandler::setInterDBDisable()
{
    if (m_bInterDBUse) {
        ConnectionPool_stop(m_ExPool);
        ConnectionPool_free(&m_ExPool);
        URL_free(&m_ExUrl);
        m_bInterDBUse = false;
    }
}

RTSTTQueItem::RTSTTQueItem(uint32_t idx, std::string callid, uint8_t spkno, std::string sttvalue, uint64_t bpos, uint64_t epos)
	:m_nDiaIdx(idx), m_sCallId(callid), m_nSpkNo(spkno), m_sSTTValue(sttvalue), m_nBpos(bpos), m_nEpos(epos)
{
}

RTSTTQueItem::~RTSTTQueItem()
{
}

JobInfoItem::JobInfoItem(std::string callid, std::string counselorcode, std::string path, std::string filename, std::string regdate, bool rxtx=false)
: m_callid(callid), m_counselorcode(counselorcode), m_path(path), m_filename(filename), m_regdate(regdate), m_rxtx(rxtx)
{

}

JobInfoItem::~JobInfoItem()
{

}