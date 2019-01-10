// stas.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#ifdef ENABLE_REALTIME
#include "CallReceiver.h"
#include "VRCManager.h"
#include "VDCManager.h"
#endif
#include "WorkTracer.h"
#include "FileHandler.h"
#include "stas.h"

#ifndef USE_ODBC
#include "DBHandler.h"
#else
#include "DBHandler_ODBC.h"
#endif

#include "HAManager.h"
#include "VFCManager.h"
#include "Notifier.h"
#include "Scheduler.h"
#include "RedisHandler.h"

#include <log4cpp/Category.hh>
#include <log4cpp/Appender.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/RollingFileAppender.hh>

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>

#include <string.h>

using namespace std;

Configuration *config;

volatile bool gRunning = true;

void term_handle(int sig)
{
    gRunning = false;
}

int main(int argc, const char** argv)
{
#if 0
	string input;
#endif
    log4cpp::Category *logger;
    DBHandler* st2db=nullptr;
    HAManager* ham = nullptr;
#ifdef ENABLE_REALTIME
    CallReceiver* rcv=nullptr;
#endif
    FileHandler* deliver = nullptr;

    int max_size = -1, max_backup = 0;
    std::string traceName;
    std::string log_max;
    
    std::signal(SIGINT, term_handle);
    std::signal(SIGTERM, term_handle);
    //std::signal(SIGABRT, term_handle);
    
    for(int i=1; i<argc; i++) {
        if(!strncmp(argv[i], "-v", 2)) {
            printf(" %s : Version (%d.%d), Build Date(%s)\n", argv[0], STAS_VERSION_MAJ, STAS_VERSION_MIN, __DATE__);
            return 0;
        }
    }
    
    try {
        config = new Configuration(argc, argv);
    } catch (std::exception &e) {
        perror(e.what());
        return -1;
    }
    
    logger = config->getLogger();
    
    traceName = config->getConfig("stas.trace_name", "worktrace.trc");
    log_max = config->getConfig("stas.trace_max", "1MiB");
    max_backup = config->getConfig("stas.trace_backup", 5);
    max_size = std::stoul(log_max.c_str()) * itfact::common::convertUnit(log_max);
    log4cpp::Appender *appender = new log4cpp::RollingFileAppender("default", traceName, max_size, max_backup);
	log4cpp::Layout *layout = new log4cpp::PatternLayout();
    try {
        ((log4cpp::PatternLayout *) layout)->setConversionPattern("[%d{%Y-%m-%d %H:%M:%S.%l}] %-6p %m%n");
        appender->setLayout(layout);
    } catch (log4cpp::ConfigureFailure &e) {
        if (layout)
            delete layout;
        layout = new log4cpp::BasicLayout();
        appender->setLayout(layout);
    }

    log4cpp::Category &tracerLog = log4cpp::Category::getInstance(std::string("WorkTracer"));
    tracerLog.addAppender(appender);
    
	logger->info("STT Tasks Allocation Server(Service) ver %d.%d BUILD : %s", STAS_VERSION_MAJ, STAS_VERSION_MIN, __DATE__);
	logger->info("================================================");
	logger->info("MPI host IP      :  %s", config->getConfig("stas.mpihost", "127.0.0.1").c_str());
	logger->info("MPI host Port    :  %d", config->getConfig("stas.mpiport", 4730));
	logger->info("MPI host Timeout :  %d", config->getConfig("stas.mpitimeout", 0));
	logger->info("Call Signal Port :  %d", config->getConfig("stas.callport", 7000));
	logger->info("Call Channel Cnt :  %d", config->getConfig("stas.channel_count", 200));
	logger->info("Voice Playtime   :  %d", config->getConfig("stas.playtime", 3));
	logger->info("Voice Begin Port :  %d", config->getConfig("stas.udp_bport", 10000));
	logger->info("Voice END Port   :  %d", config->getConfig("stas.udp_eport", 11000));

    logger->info("Database USE     :  %s", config->getConfig("database.use", "false").c_str());
    if (!config->getConfig("database.use", "false").compare("true")) {
        logger->info("Database Type    :  %s", config->getConfig("database.type", "mysql").c_str());
        logger->info("Database Addr    :  %s", config->getConfig("database.addr", "localhost").c_str());
        logger->info("Database Port    :  %s", config->getConfig("database.port", "3306").c_str());
        logger->info("Database ID      :  %s", config->getConfig("database.id", "stt").c_str());
        logger->info("Database Name    :  %s", config->getConfig("database.name", "rt_stt").c_str());
        logger->info("Database CharSet :  %s", config->getConfig("database.chset", "utf8").c_str());
        
#ifdef USE_REDIS_POOL
    // RedisHandler 활성화 옵션 체크
    if (!RedisHandler::instance()) {
        logger->error("MAIN - ERROR (Failed to get RedisHandler instance)");
        delete config;
        return -1;
    }
#endif

#ifndef USE_ODBC
        st2db = DBHandler::instance(config->getConfig("database.type", "mysql"),
                                config->getConfig("database.addr", "localhost"),
                                config->getConfig("database.port", "3306"),
                                config->getConfig("database.id", "stt"),
                                config->getConfig("database.pw", "rt_stt"),
                                config->getConfig("database.name", "rt_stt"),
                                config->getConfig("database.chset", "utf8"),
                                logger);
#else
        st2db = DBHandler::instance(config->getConfig("database.dsn", "mysql"),
                                config->getConfig("database.id", "stt"),
                                config->getConfig("database.pw", "~dltvor2009"),
                                std::stoi(config->getConfig("database.connCount", "localhost")));
#endif

        if (!st2db) {
            logger->error("MAIN - ERROR (Failed to get DBHandler instance)");
            delete config;
            return -1;
        }
    }
    logger->info("STT Result USE   :  %s", config->getConfig("stt_result.use", "true").c_str());
    logger->info("HA USE           :  %s", config->getConfig("ha.use", "true").c_str());
    if (!config->getConfig("ha.use", "true").compare("true")) {
        logger->info("HA Address       :  %s", config->getConfig("ha.addr", "192.168.0.1").c_str());
        logger->info("HA Port          :  %s", config->getConfig("ha.port", "7777").c_str());
    }

	WorkTracer::instance();
    //WorkTracer::instance()->setLogger(&tracerLog);
    
    if (!config->getConfig("stt_result.use", "false").compare("true")) {
        deliver = FileHandler::instance(config->getConfig("stt_result.path", "./stt_result")/*, logger*/);
    }

#ifdef ENABLE_REALTIME
	VRCManager* vrcm = VRCManager::instance(config->getConfig("stas.mpihost", "127.0.0.1"), config->getConfig("stas.mpiport", 4730), config->getConfig("stas.mpitimeout", 0), deliver, /*logger,*/ st2db, (config->getConfig("stas.savewav", "false").find("true")==0)?true:false, config->getConfig("stas.wavpath", "/home/stt"), config->getConfig("stas.framelen", 20), config->getConfig("stas.mode", 0));
    if (!vrcm) {
        logger->error("MAIN - ERROR (Failed to get VRCManager instance)");
        VDCManager::release();
        FileHandler::release();
        WorkTracer::release();
        delete config;
        return -1;
    }

	VDCManager* vdcm = VDCManager::instance(config->getConfig("stas.channel_count", 200), config->getConfig("stas.udp_bport", 10000), config->getConfig("stas.udp_eport", 11000), config->getConfig("stas.playtime", 3), vrcm/*, logger*/);
    if (!vdcm) {
        logger->error("MAIN - ERROR (Failed to get VDCManager instance)");
        VDCManager::release();
        FileHandler::release();
        WorkTracer::release();
        delete config;
        return -1;
    }
#endif  // ENABLE_REALTIME

	VFCManager* vfcm = VFCManager::instance(config->getConfig("stas.mpihost", "127.0.0.1"), config->getConfig("stas.mpiport", 4730), config->getConfig("stas.mpitimeout", 0)/*, logger*/);
    Notifier *noti = nullptr;
    if(vfcm) {
        noti = Notifier::instance(vfcm, st2db);
        noti->startWork();
    }

    Scheduler *schd = nullptr;
    if (st2db && vfcm) {
        schd = Scheduler::instance(st2db, vfcm);
    }

    if (!config->getConfig("ha.use", "false").compare("true")) {
#ifdef ENABLE_REALTIME
        ham = HAManager::instance(vrcm, vdcm/*, logger*/);
#else
        ham = HAManager::instance();
#endif
        if (ham->init(config->getConfig("ha.addr", "192.168.0.1"), config->getConfig("ha.port", 7777)) < 0) {
            logger->error("MAIN - ERROR (Failed to get HAManager instance)");
#ifdef ENABLE_REALTIME
            VDCManager::release();
#endif
            FileHandler::release();
            WorkTracer::release();
            HAManager::release();
            delete config;
            return -1;
        }
    }
    
#ifdef ENABLE_REALTIME
	rcv = CallReceiver::instance(vdcm, vrcm, /*logger,*/ st2db, ham);
    rcv->setNumOfExecutor(config->getConfig("stas.callexe_count", 5));

	if (!rcv->init(config->getConfig("stas.callport", 7000))) {
        goto FINISH;
	}
#endif // ENABLE_REALTIME

#if 0
    logger->debug("input waiting... quit");
	while (1) {
		std::cin >> input;
		if (!input.compare("quit")) break;
	}
#else
    while (gRunning)
    {
#ifdef ENABLE_REALTIME
        vdcm->outputVDCStat();
        vrcm->outputVRCStat();
#endif // ENABLE_REALTIME
        if (vfcm) vfcm->outputVFCStat();
        if (!config->getConfig("ha.use", "false").compare("true")) {
            ham->outputSignals();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        //std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif

#ifdef ENABLE_REALTIME
	vdcm->outputVDCStat();
	vrcm->outputVRCStat();
#endif

FINISH:

	logger->debug("MAIN FINISH!");

    if (!config->getConfig("ha.use", "false").compare("true")) {
        HAManager::release();
    }

    if (schd) {
        schd->release();
        schd = nullptr;
    }
    
    if (noti) {
        noti->stopWork();
        delete noti;
    }

    // for immediatly finish, 2018-9-6
    return 0;
    
    vfcm->release();

#ifdef ENABLE_REALTIME
	vdcm->release();
	vrcm->release();
	rcv->release();
#endif // ENABLE_REALTIME

	WorkTracer::release();
    DBHandler::release();
    if (deliver) FileHandler::release();

    delete config;

    return 0;
}

