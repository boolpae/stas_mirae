#include "stas.h"
#include "VASDivSpeaker.h"

#ifndef USE_ODBC
#include "DBHandler.h"
#else
#include "DBHandler_ODBC.h"
#endif

#include "FileHandler.h"

#include <vector>
#include <boost/algorithm/string.hpp>

#ifdef USE_RAPIDJSON
#include "rapidjson/document.h"     // rapidjson's DOM-style API
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#endif

typedef struct _stt_result {
	uint ts;
	uint te;
	std::string text;
} STTResult;

VASDivSpeaker::VASDivSpeaker(DBHandler *db, FileHandler *file, JobInfoItem *item)
: m_hDB(db), m_hFile(file), m_jobItem(item)
{

}

VASDivSpeaker::~VASDivSpeaker()
{

}

int VASDivSpeaker::startWork(gearman_client_st *gearClient, std::string &funcname, std::string &unseg)
{
    int ret=0;
    gearman_return_t rc;
    void *value = nullptr;
    size_t result_size;
    std::string sValue;
    log4cpp::Category *logger = config->getLogger();

#ifdef USE_RAPIDJSON
    {
        rapidjson::Document d;
        rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

        d.SetObject();
        d.AddMember("CMD", "SPK", alloc);
        d.AddMember("CALL-ID", rapidjson::Value(m_jobItem->getCallId().c_str(), alloc).Move(), alloc);
        d.AddMember("PATH", rapidjson::Value(m_jobItem->getPath().c_str(), alloc).Move(), alloc);
        d.AddMember("FILENAME", rapidjson::Value(m_jobItem->getFilename().c_str(), alloc).Move(), alloc);
        d.AddMember("UNSEGMENT-RESULT", rapidjson::Value(unseg.c_str(), alloc).Move(), alloc);

        rapidjson::StringBuffer strbuf;
        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
        d.Accept(writer);

        sValue = strbuf.GetString();
    }

#else
    sValue = m_jobItem->getCallId() + "," + m_jobItem->getPath() + "/" + m_jobItem->getFilename() + "\n" + unseg;
#endif  // USE_RAPIDJSON

    value= gearman_client_do(gearClient, funcname.c_str(), NULL, 
                                    (const void*)(sValue.c_str()), sValue.size(),
                                    &result_size, &rc);
    if (gearman_success(rc)) {
        std::vector<std::string> lines;
        std::vector<std::string>::iterator iter;
        std::vector<STTResult> res1;
        std::vector<STTResult> res2;
        //std::vector<STTResult>::iterator si;
        STTResult sres;
        char spker[8];
        uint nTs, nTe;


        // '\n'를 이용한 line별 구분 - vector
        // spk00, spk01 갯수 파악
        // 위에서 파악된 spk00, spk01 갯수를 이용하여 상담원과 고객 구분
        // line별(vector)로 DB에 저장
        // FullText 저장 (DB 또는 File)
#ifdef USE_RAPIDJSON
        rapidjson::Document doc;

        sValue = "";
        if (!doc.Parse((const char*)value).HasParseError() && doc["RESULT"].GetBool()) {
            sValue = doc["SPK-RESULT"].GetString();
        }
        free(value);
#else
        sValue = (const char*)value;
        free(value);
#endif  // USE_RAPIDJSON

        boost::split(lines, sValue, boost::is_any_of("\n"));

        for (iter = lines.begin(); iter != lines.end(); iter++) {
            //std::cout << *iter << std::endl;
            sscanf((*iter).c_str(), "%5s,%d,%d", spker, &nTs, &nTe);
            sres.ts = nTs;
            sres.te = nTe;
            sres.text = (*iter).substr( (*iter).find(" ") + 1 );

            if (!strncmp("spk00", spker, 5)) {
                res1.push_back(sres);
            }
            else {
                res2.push_back(sres);
            }
        }

        //"화자,시작msec,종료msec,Text"
        // 화자는 spk00, spk01 이라는 값으로 구분되며 갯수가 많은 값이 상담원이 된다. - spk00, spk01 의 갯수를 파악해야핸다.
        res1.size();
        res2.size();

        //m_hDB->insertSTTData(idx, callid, spkno, spos, epos, stt);
        
        res1.clear();
        res2.clear();
    }
    else if (gearman_failed(rc)) {
        logger->error("VASDivSpeaker::startWork() - failed gearman_client_do(%s). [%s : %s]", funcname.c_str(), m_jobItem->getCallId().c_str(), m_jobItem->getFilename().c_str());
    }

    return ret;
}

