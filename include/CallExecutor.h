#ifdef  ENABLE_REALTIME

#pragma once

#include <stdint.h>
#include <time.h>
#include <queue>
#include <mutex>

#ifdef WIN32
#include<winsock2.h>
#else

#include <netinet/in.h>

#define SOCKET int
#define closesocket close

#endif

#include <log4cpp/Category.hh>

using namespace std;

class DBHandler;

class QueueItem {
	uint16_t m_nNum;
public:
	QueueItem(uint16_t num, SOCKET sockfd, struct sockaddr_in si, time_t tm, uint16_t psize, uint8_t* packet);
	virtual ~QueueItem();

	SOCKET m_sockfd;
	struct sockaddr_in m_si;
    time_t m_time;
	uint16_t m_packetSize;
	uint8_t* m_packet;
};

class VDCManager;
class VRCManager;
class HAManager;

class CallExecutor
{
	uint16_t m_nNum;
	queue< QueueItem* > m_Que;
	static bool ms_bThrdRun;

	mutable std::mutex m_mxQue;
    
    VDCManager *m_vdcm;
    VRCManager *m_vrcm;
    
    log4cpp::Category *m_Logger;
    
    DBHandler* m_st2db;
    HAManager *m_ham;

public:
	CallExecutor(uint16_t num, VDCManager *vdcm, VRCManager *vrcm, /*log4cpp::Category *logger,*/ DBHandler* st2db, HAManager *ham=nullptr);
	virtual ~CallExecutor();

	static void thrdMain(CallExecutor* exe);
	static void thrdFinish() { ms_bThrdRun = false; }

	void pushPacket( SOCKET sockfd, struct sockaddr_in si, uint16_t psize, uint8_t* packet );
	QueueItem* popPacket();

	uint16_t getExecNum() { return m_nNum; }
};



#endif // ENABLE_REALTIME
