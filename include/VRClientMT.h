#ifdef  ENABLE_REALTIME

#ifdef USE_REALTIME_MT

#pragma once

#include <stdint.h>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <map>

#include <log4cpp/Category.hh>

#ifdef USE_REDIS_POOL
#include "xRedisClient.h"

using namespace xrc;
#endif

using namespace std;

class VRCManager;
class FileHandler;
class DBHandler;

#define VOICE_BUFF_LEN (20000)

typedef struct _queItem {
	uint8_t flag;	// 통화 시작: 2, 과 통화 중: 1, 마지막 데이터 또는 통화 종료: 0
	uint8_t spkNo;	// 통화자 번호(송신자 0, 수신자 1)
	uint32_t lenVoiceData;	// voiceData 길이
	uint8_t voiceData[VOICE_BUFF_LEN];	// 디코딩된 3초 분량의 음성 데이터
} QueItem;

class CtrlThreadInfo {
private:
	std::string ServerName;
	uint64_t TotalVoiceDataLen;
	uint32_t DiaNumber;
	uint8_t RxState;
	uint8_t TxState;
	mutable std::mutex m_mxDianum;
public:
	CtrlThreadInfo();
	virtual ~CtrlThreadInfo();

	void setServerName(std::string svrnm) { ServerName = svrnm; }
	std::string getServerName() { return ServerName; }
	void setTotalVoiceDataLen(uint64_t len) { TotalVoiceDataLen = len; }
	uint64_t getTotalVoiceDataLen() { return TotalVoiceDataLen; }
	uint32_t getDiaNumber();
	uint8_t getRxState() { return RxState; }
	void setRxState(uint8_t state) { RxState = state; }
	uint8_t getTxState() { return TxState; }
	void setTxState(uint8_t state) { TxState = state; }
};

class VRClient
{
	VRCManager* m_Mgr;

	string m_sGearHost;
	uint16_t m_nGearPort;
    int m_nGearTimeout;
	string m_sFname;
	string m_sCallId;
	string m_sCounselCode;
	volatile uint8_t m_nLiveFlag;	// Threading Class로서 객체 삭제를 thread에서 수행하도록 설계
	uint8_t m_cJobType;	// VRClient의 작업 타입(파일:F, 실시간:R)
	uint8_t m_nNumofChannel;

	queue< QueItem* > m_qRTQue;
	std::thread m_thrd;
	mutable std::mutex m_mxQue;

	queue< QueItem* > m_qRXQue;
	std::thread m_thrdRx;
	mutable std::mutex m_mxRxQue;

	queue< QueItem* > m_qTXQue;
	std::thread m_thrdTx;
	mutable std::mutex m_mxTxQue;
    
    FileHandler *m_deliver;
    log4cpp::Category *m_Logger;
    DBHandler* m_s2d;

    size_t rx_sframe;
    size_t rx_eframe;
    size_t tx_sframe;
    size_t tx_eframe;
	uint8_t syncBreak;

	uint8_t rx_hold;
	uint8_t tx_hold;

	std::string ServerName;
	uint64_t TotalVoiceDataLen;
	uint32_t DiaNumber;
	uint8_t RxState;
	uint8_t TxState;
	mutable std::mutex m_mxDianum;
	// CtrlThreadInfo thrdInfo;

    bool m_is_save_pcm;
    string m_pcm_path;

    size_t m_framelen;
	int m_mode;

	time_t m_tStart;
#ifdef EN_RINGBACK_LEN
	uint32_t m_nRingbackLen;
#endif

public:

public:
#ifdef EN_RINGBACK_LEN
	VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, /*log4cpp::Category *logger,*/ DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT, uint32_t ringbacklen);
#else
	VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, /*log4cpp::Category *logger,*/ DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT);
#endif
	void finish();

	string& getFname() { return m_sFname; }
	string& getCallId() { return m_sCallId; }
	string& getCounselCode() { return m_sCounselCode; }

	void insertQueItem(QueItem* item);

#ifdef USE_REDIS_POOL
	xRedisClient& getXRdedisClient();
#endif
#ifdef EN_RINGBACK_LEN
	uint32_t getRingbackLen() { return m_nRingbackLen; }
#endif

private:
	virtual ~VRClient();
	static void thrdMain(VRClient* client);
	static void thrdRxProcess(VRClient* client);
	static void thrdTxProcess(VRClient* client);

	// static std::map<std::string, std::shared_ptr<CtrlThreadInfo>> ThreadInfoTable;
	uint32_t getDiaNumber();
};



#endif // USE_REALTIME_MT

#endif // ENABLE_REALTIME
