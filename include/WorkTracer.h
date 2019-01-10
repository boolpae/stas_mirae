#pragma once

#include <queue>
#include <thread>
#include <mutex>
#include <string>
#include <iostream>

#include <time.h>

#include <log4cpp/Category.hh>

/*
	보통 모든 작업은 아래의 단계를 거친다.
	1. 시작		: [START]
	2. 작업 중	: [..ING]
	3. 종료		: [.STOP]

	각 단계에는 키(key)로 사용되는 call-id를 가지며 단계에 따른 태그 값과 함께 표시하도록 한다.
	예: [START] CALL-ID(기타 필요한 정보)
	예: [..ING] CALL-ID(기타 필요한 정보)

	작업 트래이스 값은 파일이나 DB에 저장할 수 있도록 interface 설계한다.
*/

class WorkQueItem {
public:
	enum PROCTYPE {
		R_BEGIN_PROC = 0,
		R_REQ_WORKER,
		R_RES_WORKER,
		R_REQ_CHANNEL,
		R_RES_CHANNEL,
		R_BEGIN_VOICE,
		R_END_PROC,
		R_END_VOICE,
		R_FREE_WORKER,
		F_BEGIN_PROC,
		F_REQ_WORKER,
		F_RES_WORKER,
		F_FREE_WORKER,
		F_END_PROC
	};

private:
	std::string m_sWorkDescription;

	time_t m_tRegTime;
	std::string m_sCallId;
	uint8_t m_cJobType;			// 'R': 실시간,  'F': 파일, 배치
	PROCTYPE m_eProcType;
	uint8_t m_nReqResult;
public:
	WorkQueItem(std::string callid, uint8_t jobType, PROCTYPE pType, uint8_t res=0);
	virtual ~WorkQueItem();

	std::string& getWorkDescription();
};

class WorkTracer
{
	static WorkTracer* ms_instance;
	bool m_bLiveFlag;

	std::queue< WorkQueItem* > m_qTraceQue;
	std::thread m_thrd;
	mutable std::mutex m_mxQue;
    
    log4cpp::Category *m_Logger;
public:
	static WorkTracer* instance();
	static void release();
    
    //void setLogger(log4cpp::Category *logger);
	
	void insertWork(std::string callid, uint8_t jobType, WorkQueItem::PROCTYPE pType, uint8_t res=0);

private:
	WorkTracer();
	virtual ~WorkTracer();

	static void thrdMain(WorkTracer* trc);

	void insertWork(WorkQueItem *item);
    
};

