
#include "stas.h"
#include "Scheduler.h"

#include "DBHandler.h"

#include "VFCManager.h"
#include "HAManager.h"

#include <chrono>

Scheduler* Scheduler::m_instance = nullptr;

Scheduler::Scheduler(DBHandler *sttdb, VFCManager *vfcmgr)
: m_bLiveFlag(true), m_sttdb(sttdb), m_vfcmgr(vfcmgr)
{
    
}

Scheduler::~Scheduler()
{
    if (m_thrd.joinable()) m_thrd.join();
    if (m_thrdUpdater.joinable()) m_thrdUpdater.join();

    m_instance = nullptr;
}

Scheduler* Scheduler::instance(DBHandler *sttdb, VFCManager *vfcmgr)
{
    if (!m_instance) {
        m_instance = new Scheduler(sttdb, vfcmgr);

        m_instance->m_thrd = std::thread(Scheduler::thrdFuncScheduler, m_instance, vfcmgr);
        m_instance->m_thrdUpdater = std::thread(Scheduler::thrdFuncStateUpdate, m_instance);
    }
    return m_instance;
}

void Scheduler::release()
{
	if (m_instance) {
        m_instance->m_thrd.detach();
        m_instance->m_thrdUpdater.detach();
        m_instance->m_bLiveFlag = false;
		delete m_instance;
	}
}

void Scheduler::thrdFuncScheduler(Scheduler *schd, VFCManager *vfcm)
{
    log4cpp::Category *logger = config->getLogger();
    HAManager *ham = HAManager::getInstance();
    std::vector< JobInfoItem* > v;
    JobInfoItem *item;

#ifdef USE_REDIS_POOL
    bool useRedisPool = !config->getConfig("redis.use", "false").compare("true") & !config->getConfig("redis.use_notify_stt", "false").compare("true");
#endif

    if (ham && !ham->getHAStat()) {
        logger->debug("Scheduler::thrdFuncScheduler() - Waiting... for Standby Mode");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    while(schd->m_bLiveFlag) {
        // DBHandler의 api를 이용하여 새로운 task를 확인
        // 새로운 task를 VFCManager의 큐에 등록

#ifdef USE_REDIS_POOL
        if (!useRedisPool) {
#endif

        // Self테이블
        if (schd->m_sttdb->getTaskInfo2(v, vfcm->getAvailableCount(), "STT_TBL_JOB_SELF_INFO") > 0) {
            for( std::vector< JobInfoItem* >::iterator iter = v.begin(); iter != v.end(); iter++) {
                item = *iter;

                logger->debug("thrdFuncScheduler (%s, %s)", item->getPath().c_str(), item->getFilename().c_str());
                // put item to VFCMgr's Queue
                if (schd->m_vfcmgr->pushItem(item/*item->getPath()+"/"+item->getFilename()*/) > 0) {
                    schd->m_sttdb->updateTaskInfo4Schd(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getTableName());
                }
            }

            v.clear();
        }

        // Retry테이블
        if (schd->m_sttdb->getTaskInfo2(v, vfcm->getAvailableCount(), "STT_TBL_JOB_RETRY_INFO") > 0) {
            for( std::vector< JobInfoItem* >::iterator iter = v.begin(); iter != v.end(); iter++) {
                item = *iter;

                logger->debug("thrdFuncScheduler (%s, %s)", item->getPath().c_str(), item->getFilename().c_str());
                // put item to VFCMgr's Queue
                if (schd->m_vfcmgr->pushItem(item/*item->getPath()+"/"+item->getFilename()*/) > 0) {
                    schd->m_sttdb->updateTaskInfo4Schd(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getTableName());
                }
            }

            v.clear();
        }

#ifdef USE_REDIS_POOL
        }
#endif
        // get items from DB
        if (schd->m_sttdb->getTaskInfo(v, vfcm->getAvailableCount(), "STT_TBL_JOB_INFO") > 0) {
            for( std::vector< JobInfoItem* >::iterator iter = v.begin(); iter != v.end(); iter++) {
                item = *iter;

                logger->debug("thrdFuncScheduler (%s, %s)", item->getPath().c_str(), item->getFilename().c_str());
                // put item to VFCMgr's Queue
                if (schd->m_vfcmgr->pushItem(item/*item->getPath()+"/"+item->getFilename()*/) > 0) {
                    schd->m_sttdb->updateTaskInfo4Schd(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getTableName());
                }
            }

            v.clear();
        }

        // config에 설정된 sec 시간 간격으로 확인
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void Scheduler::thrdFuncStateUpdate(Scheduler* schd)
{
    log4cpp::Category *logger = config->getLogger();
    HAManager *ham = HAManager::getInstance();
    JobInfoItem *item;

    if (ham && !ham->getHAStat()) {
        logger->debug("Scheduler::thrdFuncStateUpdate() - Waiting... for Standby Mode");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    schd->m_sttdb->updateAllIncompleteTask2Fail();

    while(schd->m_bLiveFlag) {
        int totalSleep = 0;
        // schd->m_sttdb->updateAllTask2Fail();
        schd->m_sttdb->updateAllTask2Fail2();
        
        while (totalSleep < 60) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!schd->m_bLiveFlag) break;
            totalSleep++;
        }

    }
}
