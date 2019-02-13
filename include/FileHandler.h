#pragma once

#include <stdint.h>

#include <string>
#include <queue>
#include <thread>
#include <mutex>

#include <log4cpp/Category.hh>

class STTQueItem {
	std::string m_sCallId;
	uint8_t m_cJobType;		// 'R': 실기간, 'F': 파일
	uint8_t m_nSpkNo;		// 1 ~ 9 : 통화자 구분값, 0 : 작업타입이 File인 경우
	std::string m_sFilename;
	std::string m_sSTTValue;

	uint64_t m_nBpos;
	uint64_t m_nEpos;

	std::string m_sCSCode;

public:
	STTQueItem(std::string callid, uint8_t jobtype, uint8_t spkno, std::string& sttvalue, uint64_t bpos, uint64_t epos, std::string cscode);
	STTQueItem(std::string callid, uint8_t jobtype, std::string filename, std::string& sttvalue);
	virtual ~STTQueItem();

	std::string& getCallId() { return m_sCallId; }
	uint8_t getJobType() { return m_cJobType; }
	uint8_t getSpkNo() { return m_nSpkNo; }
	std::string& getFilename() { return m_sFilename; }
	std::string& getSTTValue() { return m_sSTTValue; }
    uint64_t getBpos() { return m_nBpos; }
    uint64_t getEpos() { return m_nEpos; }
	std::string getCSCode() { return m_sCSCode; }
};

class FileHandler
{
	static FileHandler* ms_instance;
	
	bool m_bLiveFlag;

	std::queue< STTQueItem* > m_qSttQue;
	std::thread m_thrd;
	mutable std::mutex m_mxQue;

	// for TEST
	std::queue< STTQueItem* > m_qJsonDataQue;
	std::thread m_thrdSaveJsonData;
	mutable std::mutex m_mxJsonDataQue;

    std::string m_sResultPath;
    
    log4cpp::Category *m_Logger;
    
public:
	static FileHandler* instance(std::string path/*, log4cpp::Category *logger*/);
	static void release();
	static FileHandler* getInstance();

	void insertSTT(std::string callid, std::string& stt, uint8_t spkNo, uint64_t bpos, uint64_t epos, std::string cscode);		// for Realtime
	void insertSTT(std::string callid, std::string& stt, std::string filename);	// for FILE, BATCH
	// for TEST
	void insertJsonData(std::string callid, std::string& stt, uint8_t spkNo, uint64_t bpos, uint64_t epos, std::string cscode);		// for Realtime

private:
	FileHandler(std::string path/*, log4cpp::Category *logger*/);
	virtual ~FileHandler();

	static void thrdMain(FileHandler* dlv);
	// for TEST
	static void thrdSaveJsonData(FileHandler* dlv);

	void insertSTT(STTQueItem* item);
	// for TEST
	void insertJsonData(STTQueItem* item);
};

