#ifdef  ENABLE_REALTIME

#include "DivSpkManager.h"

#include <boost/algorithm/string.hpp>


typedef struct _stt_result {
	uint ts;
	uint te;
	std::string text;
} STTResult;

DivSpkManager *DivSpkManager::m_instance = nullptr;


DivSpkManager::DivSpkManager()
{

}

DivSpkManager::~DivSpkManager()
{
    if (m_instance) {
        delete m_instance;
    }
}

DivSpkManager* DivSpkManager::instance()
{
    if (m_instance) return m_instance;

    m_instance = new DivSpkManager();

    return m_instance;
}

int DivSpkManager::doDivSpeaker(std::string callid, std::string &sttValue)
{
    int ret=0;
    std::string existSttValue;
    //td::string newSttValue;

    // Search CallId from Vector : 
    ret = getSttValue(callid, existSttValue);

    // if exists not callid : 
    if ( existSttValue.size() == 0 ) {
    // insert callid, sttValue and return : 
        insertSttValue(callid, sttValue);
    }
    else {
        // doDivSpeaker
        std::vector<std::string> lines;
        std::vector<std::string>::iterator iter;
        std::vector<STTResult> res1;
        std::vector<STTResult> res2;
        STTResult sres;
        uint nTs, nTe;

        boost::split(lines, existSttValue, boost::is_any_of("\n"));
        for (iter = lines.begin(); iter != lines.end(); iter++) {
            //std::cout << *iter << std::endl;
            sscanf((*iter).c_str(), "%d,%d", &nTs, &nTe);
            sres.ts = nTs;
            sres.te = nTe;
            sres.text = (*iter).substr( (*iter).find(" ") + 1 );
            res1.push_back(sres);
        }

        boost::split(lines, sttValue, boost::is_any_of("\n"));
        for (iter = lines.begin(); iter != lines.end(); iter++) {
            //std::cout << *iter << std::endl;
            sscanf((*iter).c_str(), "%d,%d", &nTs, &nTe);
            sres.ts = nTs;
            sres.te = nTe;
            sres.text = (*iter).substr( (*iter).find(" ") + 1 );
            res2.push_back(sres);
        }

        //"화자,시작msec,종료msec,Text"
        // 화자는 spk00, spk01 이라는 값으로 구분되며 갯수가 많은 값이 상담원이 된다. - spk00, spk01 의 갯수를 파악해야핸다.
        res1.size();
        res2.size();
        
        res1.clear();
        res2.clear();
    }

    return ret;
}

int DivSpkManager::insertSttValue(std::string callid, std::string &sttValue)
{
    SpkItem item;

    item.callid = callid;
    item.sttvalue = sttValue;

    std::lock_guard<std::mutex> g(m_mxQue);

    m_vSpkItems.push_back(item);

    return m_vSpkItems.size();
}

int DivSpkManager::getSttValue(std::string callid, std::string &sttValue)
{
    // Lock
    std::lock_guard<std::mutex> g(m_mxQue);
    std::vector< SpkItem >::iterator iter;

    for(iter = m_vSpkItems.begin(); iter != m_vSpkItems.end(); iter++) {
        if ((*iter).callid == callid) {
            sttValue = (*iter).sttvalue;
            m_vSpkItems.erase(iter);
            break;
        }
    }

    return m_vSpkItems.size();
}

void DivSpkManager::clearSttValue(std::string callid) {
    // Lock
    std::lock_guard<std::mutex> g(m_mxQue);
    std::vector< SpkItem >::iterator iter;

    for(iter = m_vSpkItems.begin(); iter != m_vSpkItems.end(); iter++) {
        if ((*iter).callid == callid) {
            m_vSpkItems.erase(iter);
            break;
        }
    }
}


#endif // ENABLE_REALTIME