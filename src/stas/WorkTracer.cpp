
#include "WorkTracer.h"
#include "configuration.h"
#include "stas.h"

#include <iostream>
#include <thread>

WorkTracer* WorkTracer::ms_instance = NULL;

WorkTracer::WorkTracer()
	: m_bLiveFlag(true)/*, m_Logger(NULL)*/
{
	m_Logger = config->getLogger();
	std::cout << "\t[DEBUG] WorkTracer Constructed." << std::endl;
}


WorkTracer::~WorkTracer()
{
	std::cout << "\t[DEBUG] WorkTracer Destructed." << std::endl;
}

void WorkTracer::thrdMain(WorkTracer * trc)
{
	WorkQueItem *item;
	std::lock_guard<std::mutex> *g;// (trc->m_mxQue);
	while (trc->m_bLiveFlag) {
		while (!trc->m_qTraceQue.empty()) {
			g = new std::lock_guard<std::mutex>(trc->m_mxQue);
			item = trc->m_qTraceQue.front();
			trc->m_qTraceQue.pop();
			delete g;
#ifdef TEMP_DISABLE
            if (!trc->m_Logger) {
                // item을 이용하여 처리
                std::cout << item->getWorkDescription() << std::endl;
            }
            else {
                trc->m_Logger->info(item->getWorkDescription());
			}
#endif // TEMP_DISABLE
			delete item;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

WorkTracer* WorkTracer::instance()
{
	if (ms_instance) return ms_instance;

	ms_instance = new WorkTracer();
    
	ms_instance->m_thrd = std::thread(WorkTracer::thrdMain, ms_instance);
	//thrd.detach();

	return ms_instance;
}

void WorkTracer::release()
{
	ms_instance->m_bLiveFlag = false;

	ms_instance->m_thrd.join();

	delete ms_instance;
	ms_instance = NULL;
}

void WorkTracer::insertWork(WorkQueItem* item)
{
	std::lock_guard<std::mutex> g(m_mxQue);
	m_qTraceQue.push(item);
}

void WorkTracer::insertWork(std::string callid, uint8_t jobType, WorkQueItem::PROCTYPE pType, uint8_t res)
{
	WorkQueItem* item = new WorkQueItem(callid, jobType, pType, res);
	insertWork(item);
}

WorkQueItem::WorkQueItem(std::string callid, uint8_t jobType, PROCTYPE pType, uint8_t res)
	: m_sCallId(callid), m_cJobType(jobType), m_eProcType(pType), m_nReqResult(res)
{
	m_tRegTime = time(NULL);
}

WorkQueItem::~WorkQueItem()
{
}

std::string & WorkQueItem::getWorkDescription()
{
	struct tm *timeinfo;
	char sTimeinfo[32];

	
	// TODO: 여기에 반환 구문을 삽입합니다.
	timeinfo = localtime(&m_tRegTime);
	::strftime(sTimeinfo, sizeof(sTimeinfo), "%F %T", timeinfo);

	m_sWorkDescription.clear();
	m_sWorkDescription += "[";
	m_sWorkDescription += sTimeinfo;
	m_sWorkDescription += "] [";
	m_sWorkDescription += m_sCallId;
	m_sWorkDescription += " - ";

	if (m_cJobType == 'R') m_sWorkDescription += "REALTIME] ";
	else m_sWorkDescription += "FILE-BAT] ";

	switch (m_eProcType) {
	case R_BEGIN_PROC:
		m_sWorkDescription += "Begin Realtime Proc.(Input Begin Call Signal.)";
		break;
	case R_REQ_WORKER:
		m_sWorkDescription += "Request Worker for Realtime.";
		break;
	case R_RES_WORKER:
		m_sWorkDescription += "Result(Request Worker for Realtime) : ";
		if (m_nReqResult) m_sWorkDescription += "(SUCCESS)";
		else m_sWorkDescription += "(FAIL)";
		break;
	case R_REQ_CHANNEL:
		m_sWorkDescription += "Request Channel for Realtime.";
		break;
	case R_RES_CHANNEL:
		m_sWorkDescription += "Result(Request Channel for Realtime) : ";
		if (m_nReqResult) m_sWorkDescription += "(SUCCESS)";
		else m_sWorkDescription += "(FAIL)";
		break;
	case R_BEGIN_VOICE:
		m_sWorkDescription += "Input First Voice Data(SPKNO:";
		m_sWorkDescription += std::to_string(m_nReqResult);
		m_sWorkDescription += ")";
		break;
	case R_END_PROC:
		m_sWorkDescription += "End Realtime Proc.(Input End Call Signal.)";
		break;
	case R_END_VOICE:
		m_sWorkDescription += "Input End Call Signal(SPKNO:";
		m_sWorkDescription += std::to_string(m_nReqResult);
		m_sWorkDescription += ")";
		break;
	case R_FREE_WORKER:
		m_sWorkDescription += "Free Worker for Realtime.";
		break;
	case F_BEGIN_PROC:
		m_sWorkDescription += "Begin File(Batch) Proc.";
		break;
	case F_REQ_WORKER:
		m_sWorkDescription += "Request Worker for File(Batch).";
		break;
	case F_RES_WORKER:
		m_sWorkDescription += "Result(Request Worker for File) : ";
		if (m_nReqResult) m_sWorkDescription += "(SUCCESS)";
		else m_sWorkDescription += "(FAIL)";
		break;
	case F_FREE_WORKER:
		m_sWorkDescription += "Free Worker for File(Batch).";
		break;
	case F_END_PROC:
		m_sWorkDescription += "End File(Batch) Proc.";
		break;
	default:
		m_sWorkDescription += "Unknown WorkType.(";
		m_sWorkDescription += std::to_string(m_eProcType);
		m_sWorkDescription += ")";
		break;
	}

	return m_sWorkDescription;
}

#if 0
void WorkTracer::setLogger(log4cpp::Category *logger)
{
    m_Logger = logger;
}
#endif