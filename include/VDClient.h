#ifdef  ENABLE_REALTIME

#pragma once

#include <stdint.h>
#include <string>
#include <thread>

#ifdef WIN32
#include<winsock2.h>
#else

#define SOCKET int
#define closesocket close

#endif

#include <log4cpp/Category.hh>

class VRClient;
class VRCManager;

class VDClient
{
	volatile uint8_t m_nLiveFlag;	// Threading Class로서 객체 삭제를 thread에서 수행하도록 설계
									// 스레드 수명 - 실행중: 1, 종료: 0
	volatile uint8_t m_nWorkStat;	// Threading Class로서 자체 스레드의 수행 상태를 결정
									// 스레드 수행 상태 - 대기: 0, 작업중: 1, 종료 요청: 2

	uint16_t m_nPort;				// UDP Port
	SOCKET m_nSockfd;
	std::string m_sCallId;
	std::string m_sCounselCode;
	volatile uint8_t m_nSpkNo;


	VRClient* m_pVrc;

	time_t m_tTimeout;

	std::thread m_thrd;
    
    VRCManager *m_vrcm;
    
    log4cpp::Category *m_Logger;
    
    uint32_t m_nPlaytime;
    
public:
	VDClient(VRCManager *vrcm/*, log4cpp::Category *logger*/);
	void finish();

	// return:	호출 성공 시 : 0 반환, 실패 시 0 이 아닌 양수 값
	uint16_t init(uint16_t port);

	void setCallId(std::string& callid) { m_sCallId = callid; }
	std::string& getCallId() { return m_sCallId; }

	void setSpkNo(uint8_t no) { m_nSpkNo = no; }
	uint8_t getSpkNo() { return m_nSpkNo; }

	uint16_t getPort() { return m_nPort; }
	uint8_t getWorkStat() { return m_nWorkStat; }

	void startWork( std::string& callid, std::string& counselcode, uint8_t spkno );
	void stopWork() { m_nWorkStat = uint8_t(2); }

	void setVRClient(VRClient* vrc) { m_pVrc = vrc; }
    void setPlaytime(uint32_t pt) { m_nPlaytime = pt * 16000; m_nPlaytime = 19200; }    // 16 * 1200 : 1.2 sec ( 20 and 30 frames )

private:
	static void thrdMain(VDClient* client);
	virtual ~VDClient();
};


#endif // ENABLE_REALTIME
