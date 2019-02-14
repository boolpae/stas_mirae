

#include "FileHandler.h"
#include "stas.h"

#include <thread>
#include <iostream>
#include <fstream>

#include <iconv.h>
#include <string.h>
#include <unistd.h>

#include "Utils.h"
#include <time.h>

FileHandler* FileHandler::ms_instance = NULL;

FileHandler::FileHandler(std::string path/*, log4cpp::Category *logger*/)
	: m_bLiveFlag(true), m_sResultPath(path)/*, m_Logger(logger)*/
{
    m_Logger = config->getLogger();
	m_Logger->debug("FileHandler Constructed.");
}


FileHandler::~FileHandler()
{
	m_Logger->debug("FileHandler Destructed.");
}

// #define ENC_UTF8

void FileHandler::thrdMain(FileHandler * dlv)
{
	std::lock_guard<std::mutex> *g;// (m_mxQue);
	STTQueItem* item;
	std::string sttFilename;
    std::string fullpath;
    int ret=0;
#ifdef FOR_TEST
    std::string testSttFilename;
#endif

    char datebuff[32];
    struct tm * timeinfo;
    time_t currTm;

#ifdef ENC_UTF8
    char *utf_buf = NULL;
    size_t in_size, out_size;
    iconv_t it;
    char *input_buf_ptr = NULL;
    char *output_buf_ptr = NULL;
#endif

	while (dlv->m_bLiveFlag) {
		while (!dlv->m_qSttQue.empty()) {
			g = new std::lock_guard<std::mutex>(dlv->m_mxQue);
			item = dlv->m_qSttQue.front();
			dlv->m_qSttQue.pop();
			delete g;

#ifdef ENC_UTF8
            in_size = item->getSTTValue().size();
            out_size = item->getSTTValue().size() * 2;
            utf_buf = (char *)malloc(out_size);
            
            if (utf_buf) {
                memset(utf_buf, 0, out_size);

                input_buf_ptr = (char *)item->getSTTValue().c_str();
                output_buf_ptr = utf_buf;

                it = iconv_open("UTF-8", "EUC-KR");

                ret = iconv(it, &input_buf_ptr, &in_size, &output_buf_ptr, &out_size);
                 
                iconv_close(it);
            }
            else {
                utf_buf = nullptr;
                ret = -1;
            }
#endif
            currTm = time(NULL);
            timeinfo = localtime(&currTm);
            strftime (datebuff,sizeof(datebuff),"%Y%m%d",timeinfo);
			if (item->getJobType() == 'R') {
                fullpath = dlv->m_sResultPath + "/REALTIME/" + datebuff + "/" + item->getCSCode() + "/";
                if ( access(fullpath.c_str(), F_OK) ) {
                    MakeDirectory(fullpath.c_str());
                }
            }
            else
            {
                fullpath = dlv->m_sResultPath + "/FILETIME/" + datebuff + "/" ;
                if ( access(fullpath.c_str(), F_OK) ) {
                    MakeDirectory(fullpath.c_str());
                }
            }

			// item으로 로직 수행
			if (item->getJobType() == 'R') {
				sttFilename = fullpath + item->getCSCode() + "_" + item->getCallId();
#ifdef FOR_TEST
				testSttFilename = fullpath + item->getCSCode() + "_" + item->getCallId() + "_test.stt";
#endif
                /*
				sttFilename += "_";
				sttFilename += std::to_string(item->getSpkNo());
                 */
                if (item->getSpkNo() == 1) {
                    sttFilename += "_r.stt";
                }
                else if (item->getSpkNo() == 2) {
                    sttFilename += "_l.stt";
                }
                else {
                    sttFilename += ".stt";
                }
				
			}
			else {
				sttFilename = fullpath + item->getFilename();// dlv->m_sResultPath + "/" + item->getCallId();
				sttFilename += ".stt";
			}
			std::ofstream sttresult(sttFilename, std::ios::out | std::ios::app);
			if (sttresult.is_open()) {
#if 0
                if (item->getJobType() == 'R') {
                    if (item->getSpkNo() == 1) {
                        sttresult << "<< Counselor >> : ";
                    }
                    else if (item->getSpkNo() == 2) {
                        sttresult << "<< Customer >> : ";
                    }
                    /*
                    else {
                        sttresult << "<< BATCH >> : ";
                    }
                    */
                    sttresult << std::to_string(item->getBpos()) << " - " << std::to_string(item->getEpos()) << std::endl;
                }
#endif

#ifdef ENC_UTF8
				sttresult << ((ret == -1) ? item->getSTTValue() : utf_buf);//item->getSTTValue();
#else
                sttresult << item->getSTTValue() ;//item->getSTTValue();
#endif
                /*
                if (item->getJobType() == 'R') {
                    sttresult << std::to_string(item->getEpos()) << std::endl;
                }
                 */
                sttresult << std::endl;
				sttresult.close();
			}
#ifdef ENC_UTF8
            if (utf_buf) free(utf_buf);
#endif

#ifdef FOR_TEST
			std::ofstream testSttresult(testSttFilename, std::ios::out | std::ios::app);
			if (testSttresult.is_open()) {

                if (item->getJobType() == 'R') {
                    if (item->getSpkNo() == 1) {
                        testSttresult << "<< COUNSELOR >> : ";
                    }
                    else if (item->getSpkNo() == 2) {
                        testSttresult << "<< CUSTOMER >> : ";
                    }

                    testSttresult << std::to_string(item->getBpos()) << " - " << std::to_string(item->getEpos()) << std::endl;
                }

                testSttresult << item->getSTTValue() ;

                // if (item->getJobType() == 'R') {
                //     testSttresult << std::to_string(item->getEpos()) << std::endl;
                // }

                testSttresult << std::endl;
				testSttresult.close();
			}
#endif // FOR_TEST

			delete item;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

// for TEST
void FileHandler::thrdSaveJsonData(FileHandler * dlv)
{
	std::lock_guard<std::mutex> *g;// (m_mxQue);
	STTQueItem* item;
	std::string sttFilename;
    std::string fullpath;
    int ret=0;

    char datebuff[32];
    struct tm * timeinfo;
    time_t currTm;


	while (dlv->m_bLiveFlag) {
		while (!dlv->m_qJsonDataQue.empty()) {
			g = new std::lock_guard<std::mutex>(dlv->m_mxJsonDataQue);
			item = dlv->m_qJsonDataQue.front();
			dlv->m_qJsonDataQue.pop();
			delete g;

            currTm = time(NULL);
            timeinfo = localtime(&currTm);
            strftime (datebuff,sizeof(datebuff),"%Y%m%d",timeinfo);
			if (item->getJobType() == 'R') {
                fullpath = dlv->m_sResultPath + "/REALTIME/" + datebuff + "/" + item->getCSCode() + "/";
                if ( access(fullpath.c_str(), F_OK) ) {
                    MakeDirectory(fullpath.c_str());
                }
            }
            else
            {
                delete item;
                continue;
            }

			// item으로 로직 수행
            sttFilename = fullpath + item->getCSCode() + "_" + item->getCallId();
            sttFilename += ".json";

			std::ofstream sttresult(sttFilename, std::ios::out | std::ios::app);
			if (sttresult.is_open()) {
                if (item->getSpkNo() == 0) sttresult << "[";
                else if (item->getSpkNo() == 1) sttresult << ",";

                sttresult << item->getSTTValue() ;

                if (item->getSpkNo() == 2) sttresult << "]";
				sttresult.close();
			}


			delete item;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

// desc:	spkNo의 값이 0인 경우 실시간 STT 결과값이 아님(이 경우 jobtype의 값도 'F' 이어야 함)
//			실시간 STT 결과인 경우 jobtype : 'R' 이며 spkNo의 값도 1 이상의 값이어야 함
void FileHandler::insertSTT(std::string callid, std::string& stt, uint8_t spkNo, uint64_t bpos, uint64_t epos, std::string cscode)
{
	insertSTT(new STTQueItem(callid, uint8_t('R'), spkNo, stt, bpos, epos, cscode));
}

void FileHandler::insertSTT(std::string callid, std::string& stt, std::string filename)
{
	insertSTT(new STTQueItem(callid, uint8_t('F'), filename, stt));
}

void FileHandler::insertSTT(STTQueItem * item)
{
	std::lock_guard<std::mutex> g(m_mxQue);
	m_qSttQue.push(item);
}

// for TEST
void FileHandler::insertJsonData(std::string callid, std::string& stt, uint8_t spkNo, uint64_t bpos, uint64_t epos, std::string cscode)
{
	insertJsonData(new STTQueItem(callid, uint8_t('R'), spkNo, stt, bpos, epos, cscode));
}
void FileHandler::insertJsonData(STTQueItem * item)
{
	std::lock_guard<std::mutex> g(m_mxJsonDataQue);
	m_qJsonDataQue.push(item);
}



FileHandler* FileHandler::instance(std::string path/*, log4cpp::Category *logger*/)
{
	if (ms_instance) return ms_instance;
    
    if ( ::access(path.c_str(), 0) ) {
        std::string cmd = "mkdir -p ";
        cmd += path;
        std::system(cmd.c_str());
        
        if ( ::access(path.c_str(), 0) ) {
            log4cpp::Category *logger = config->getLogger();
            logger->error("FileHandler::instance - failed create path : %s", path.c_str());
            return nullptr;
        }
    }

	ms_instance = new FileHandler(path/*, logger*/);

	ms_instance->m_thrd = std::thread(FileHandler::thrdMain, ms_instance);
    // for TEST
	ms_instance->m_thrdSaveJsonData = std::thread(FileHandler::thrdSaveJsonData, ms_instance);

	return ms_instance;
}

FileHandler* FileHandler::getInstance()
{
    if(ms_instance) return ms_instance;
    return nullptr;
}

void FileHandler::release()
{
    if (ms_instance) {
        ms_instance->m_bLiveFlag = false;

        ms_instance->m_thrd.join();
        // for TEST
        ms_instance->m_thrdSaveJsonData.join();

        delete ms_instance;
        ms_instance = NULL;
        
    }
}

STTQueItem::STTQueItem(std::string callid, uint8_t jobtype, uint8_t spkno, std::string& sttvalue, uint64_t bpos, uint64_t epos, std::string cscode)
	:m_sCallId(callid), m_cJobType(jobtype), m_nSpkNo(spkno), m_sFilename(""), m_sSTTValue(sttvalue), m_nBpos(bpos), m_nEpos(epos), m_sCSCode(cscode)
{
}

STTQueItem::STTQueItem(std::string callid, uint8_t jobtype, std::string filename, std::string& sttvalue)
	:m_sCallId(callid), m_cJobType(jobtype), m_nSpkNo(0), m_sFilename(filename), m_sSTTValue(sttvalue)
{
}

STTQueItem::~STTQueItem()
{
}
