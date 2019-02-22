
#include "stas.h"
#include "VFClient.h"
#include "VFCManager.h"
#include "DivSpkManager.h"

#include "DBHandler.h"

#include "FileHandler.h"
#include "VASDivSpeaker.h"

#include <thread>

#include <string.h>

#include <boost/algorithm/string.hpp>
#include <sstream>
#include <fstream>


#include "rapidjson/document.h"     // rapidjson's DOM-style API
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"


VFClient::VFClient(VFCManager* mgr, std::string gearHost, uint16_t gearPort, int gearTimeout, uint64_t numId, bool bMakeMLF, std::string resultpath)
: m_LiveFlag(true), m_mgr(mgr), m_sGearHost(gearHost), m_nGearPort(gearPort), m_nGearTimeout(gearTimeout), m_nNumId(numId), m_bMakeMLF(bMakeMLF), m_sResultPath(resultpath)
{
    
}

VFClient::~VFClient()
{
    if (m_thrd.joinable()) m_thrd.join();
}

void VFClient::startWork()
{
    m_thrd = std::thread(VFClient::thrdFunc, m_mgr, this);
}

void VFClient::thrdFunc(VFCManager* mgr, VFClient* client)
{
    gearman_client_st *gearClient;
    gearman_return_t ret;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;

    char buf[256];
    int buflen=0;
    
    std::string reqFilePath;
    std::string line;

    JobInfoItem* item;
    
    str:string sValue;
    std::string sFuncName="";
    std::string svr_name="DEFAULT";
    std::string err_code="";
    size_t nPos1=0, nPos2=0;
    int nFilesize=0;

    DBHandler *DBHandler = DBHandler::getInstance();
    FileHandler *FileHandler = FileHandler::getInstance();
    log4cpp::Category *logger = config->getLogger();

    if (!DBHandler) {
        logger->error("VFClient::thrdFunc(%ld) - Failed to get DBHandler instance", client->m_nNumId);
        return;
    }

#if 1
    gearClient = gearman_client_create(NULL);
    if (!gearClient) {
        //printf("\t[DEBUG] VRClient::thrdMain() - ERROR (Failed gearman_client_create - %s)\n", client->m_sCallId.c_str());
        //client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed gearman_client_create - %s)", client->m_sCallId.c_str());

        //WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        client->m_thrd.detach();
        delete client;
        return;
    }
    
    ret= gearman_client_add_server(gearClient, client->m_sGearHost.c_str(), client->m_nGearPort);
    if (gearman_failed(ret))
    {
        //printf("\t[DEBUG] VRClient::thrdMain() - ERROR (Failed gearman_client_add_server - %s)\n", client->m_sCallId.c_str());
        //client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed gearman_client_add_server - %s)", client->m_sCallId.c_str());

        //WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);
        gearman_client_free(gearClient);
        client->m_thrd.detach();
        delete client;
        return;
    }

    if (client->m_nGearTimeout) {
        gearman_client_set_timeout(gearClient, client->m_nGearTimeout);
    }
#endif

    while(client->m_LiveFlag) {
        if(item = mgr->popItem()) {
//             struct tm tm;
// #if defined(USE_ORACLE) || defined(USE_TIBERO)
//             strptime(item->getRegdate().c_str(), "%Y/%m/%d %H:%M:%S", &tm);
// #else
//             strptime(item->getRegdate().c_str(), "%Y-%m-%d %H:%M:%S", &tm);
// #endif
//             time_t startT = mktime(&tm);
#ifdef USE_RAPIDJSON
            {
                rapidjson::Document d;
                rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

                d.SetObject();
                d.AddMember("CMD", "STT", alloc);
                d.AddMember("PROTOCOL", "FTP", alloc);
                d.AddMember("PATH", rapidjson::Value(item->getPath().c_str(), alloc).Move(), alloc);
                d.AddMember("FILENAME", rapidjson::Value(item->getFilename().c_str(), alloc).Move(), alloc);
                d.AddMember("CALL-ID", rapidjson::Value(item->getCallId().c_str(), alloc).Move(), alloc);

                {
                    rapidjson::Value account;

                    account.SetObject();

                    account.AddMember("ID", "boolpae", alloc);
                    account.AddMember("PW", "password", alloc);

                    d.AddMember("ACCOUNT", account, alloc);
                }

                rapidjson::StringBuffer strbuf;
                rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                d.Accept(writer);

                reqFilePath = strbuf.GetString();
            }

#else
            reqFilePath = item->getPath() + "/" + item->getFilename();
            logger->debug("VFClient::thrdFunc(%ld) - FilePath(%s)", client->m_nNumId, reqFilePath.c_str());

#endif
            memset(buf, 0, sizeof(buf));
            buflen = 0;
            
            auto t1 = std::chrono::high_resolution_clock::now();
#if 1
            // 1. Start STT : JOB_STT
            value= gearman_client_do(gearClient, "vr_stt", NULL, 
                                            (const void*)reqFilePath.c_str(), reqFilePath.size(),
                                            &result_size, &rc);
            if (gearman_success(rc)) {
                // Make use of value
                if (value) {
                    //std::string sValue((const char*)value);
                    sValue = (const char*)value;
                    // std::cout << "STT RESULT <<\n" << (const char*)value << "\n>>" << std::endl;
                    // value의 값이 '{spk_flag'로 시작될 경우 화자 분리 로직으로 처리
                    // 화자 분리 또는 일반의 경우 동일하게 unsegment까지 우선 진행되어야 한다.
                    // parse a result's header {} or filesize
#if 0
                    1. parse header - '{spk_flag' 문구가 있으면 화자 분리, 없으면 일반
                    2. cond.(화자분리), 필요한 인자값 수집 - spk_node 에 설정된 gearman function이름 값을 가져온다.
                    3. unsegment - 화자분리가 아닐 경우 이 단계에서 종료 - 화자 분리 여부에 관계없이 수행된다.
                    4. cond.(화자분리), 화자분리 시 2.에서 수집한 인자값을 이용하여 화자분리 수행
                    5. 화자 분리 결과 처리
#endif

                    free(value);

#ifdef USE_RAPIDJSON
                    {
                        rapidjson::Document doc;

                        if (!doc.Parse(sValue.c_str()).HasParseError() && doc["RESULT"].GetBool()) {
                            rapidjson::Value& res = doc["STT-RESULT"];

                            sFuncName = doc["FUNC-NAME"].GetString();

                            {
                                rapidjson::Value& resStts = res["VALUES"];

                                resStts.IsArray();

                                res["CHANNEL-COUNT"].GetInt();
                                res["SPK-COUNT"].GetInt();

                                // 2ch waves
                                // mono wave

                                // if ( res["CHANNEL-COUNT"].GetInt() == 2 ) {}
                                for (rapidjson::SizeType i = 0; i < resStts.Size(); i++) {// Uses SizeType instead of size_t
                                    rapidjson::Document d;
                                    rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

                                    d.SetObject();
                                    d.AddMember("CMD", "UNSEGMENT", alloc);
                                    d.AddMember("CALL-ID", rapidjson::Value(item->getCallId().c_str(), alloc).Move(), alloc);
                                    d.AddMember("STT-RESULT", rapidjson::Value(resStts[i].GetString(), alloc).Move(), alloc);

                                    rapidjson::StringBuffer strbuf;
                                    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                    d.Accept(writer);

                                    reqFilePath = strbuf.GetString();
                                    //printf("a[%d] = %s\n", i, resStts[i].GetString());
                                    value= gearman_client_do(gearClient, "vr_text", NULL, 
                                                                    strbuf.GetString(), strbuf.GetLength(),
                                                                    &result_size, &rc);
                                    if (gearman_success(rc)) {
                                        // Make use of value
                                        if (value) {
                                            uint32_t diaNumber=0;
                                            std::string strValue((const char*)value);

                                            free(value);

                                            //std::cout << "STT RESULT <<\n" << (const char*)value << "\n>>" << std::endl;
                                            // Unsegment 결과를 정제(parsing)하여 목적에 따라 처리한다.
                                            rapidjson::Document docUnseg;

                                            if (!docUnseg.Parse(strValue.c_str()).HasParseError() && docUnseg["RESULT"].GetBool()) {
                                                std:string sttValue(docUnseg["UNSEGMENT-RESULT"].GetString());
                                                // # 화자 분리
                                                if (res["CHANNEL-COUNT"].GetInt()==1 && res["SPK-COUNT"].GetInt()==2) {
                                                    VASDivSpeaker divspk(DBHandler, FileHandler, item);

                                                    //startWork(gearman_client_st *gearClient, std::string &funcname, std::string &unseg);
                                                    divspk.startWork(gearClient, sFuncName, sttValue);
                                                }
                                                else if (item->getRxTxType()){
                                                    // 
                                                    DivSpkManager *pDSM = DivSpkManager::instance();

                                                    pDSM->doDivSpeaker(item->getCallId(), sttValue);
                                                }
                                                else {
                                                    std::istringstream iss(sttValue);
                                                    std::vector<std::string> strs;
                                                    while(std::getline(iss, line)) {
                                                        boost::split(strs, line, boost::is_any_of(","));
                                                        //std::cout << "[1] : " << strs[0] << " [2] : " << strs[1] << " [3] : " << strs[2] << std::endl;

                                                        // to DB
                                                        if (DBHandler) {
                                                            diaNumber++;
                                                            DBHandler->insertSTTData(diaNumber, item->getCallId(), 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4), strs[2]);
                                                        }

                                                        // to STTDeliver(file), FullText
                                                        if (FileHandler) {
                                                            //FileHandler->insertSTT(item->getCallId(), strs[2], 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4));
                                                            FileHandler->insertSTT(item->getCallId(), strs[2], item->getCallId());
                                                        }
                                                    }
                                                }
                                            }
                                            else { // if Unsegment Result is False ...

                                            }
                                            // DBHandler에서 처리할 수 있도록... VRClient와 동일하게?
                                            // 그럼... 전체 STT결과 처리는?

                                        }
                                        auto t2 = std::chrono::high_resolution_clock::now();
                                        DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'Y', nFilesize, nFilesize/16000, std::chrono::duration_cast<std::chrono::seconds>(t2-t1).count(), item->m_procNo, item->getTableName().c_str());
                                    }
                                    else if (gearman_failed(rc)) {
                                        logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text). [%s : %s], timeout(%d)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout);
                                        DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), "E20400"/*gearman_client_error(gearClient)*/);
                                    }

                                }
                            }

                        }
                    }
#else   // USE_RAPIDJSON
                    if (sValue[0] == 'E' && sValue[0] != '{') {
                        err_code = sValue.substr(0, sValue.find("\n"));
                        svr_name = sValue.substr(sValue.find("\n")+1);
                        logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_stt). [%s : %s], error_code(%s), server_name(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), err_code.c_str(), svr_name.c_str());
                        DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), err_code.c_str(), svr_name.c_str());
                    }
                    else
                    // 2CH Wave...include RX,TX
                    if (sValue.find("||") != string::npos) {
                        /*
                         * 2Ch wave의 경우 하나의 wav파일에 rx,tx음성이 함께 들어있다.
                         * 이 경우 rx,tx에 대한 stt결과가 '||' 구분자로 구분되어 함께 전송된다.
                         * sValue에서 '||' 구분자를 이용하여 rx,tx를 분리하여 unsegment를 진행한다.
                         * rx,tx에 대해 두 번의 unsegment를 진행하는데 한 번이라도 실패할 경우 실패로 간주한다.
                         * 두 번의 unsegment가 성공할 경우 각 각의 unsegment 결과에서 상담원, 고객을 알아낸다.(라인갯수)
                         * 상담원, 고객으로 구별된 데이터를 DB에 저장 후 마무리
                         */
                        std::string rx_unseg;
                        std::string tx_unseg;
                        nPos1 = 0;
                        nPos2 = 0;
                        
                        nPos2 = sValue.find("\n", nPos1) + 1;    // RES_CODE

                        nPos1 = nPos2;
                        nPos2 = sValue.find("\n", nPos1) + 1;
                        svr_name = sValue.substr(nPos1, nPos2-nPos1-1);   // SERVER_NAME

                        nPos1 = nPos2;
                        nPos2 = sValue.find("\n", nPos1) + 1;
                        nFilesize = std::stoi(sValue.substr(nPos1, nPos2-nPos1-1));   // FILE_SIZE

                        nPos1 = nPos2;
                        nPos2 = sValue.find("||", nPos1);
                        std::string tx(sValue.substr(nPos1, nPos2-nPos1));    // 고객
                        std::string rx(sValue.substr(nPos2+2));     // 상담원

                        // MLF 파일 저장 옵션
                        if (client->m_bMakeMLF) {
                            std::string mlfFilename;
                            // save MLF to MLF-File
                            // rx, tx
                            mlfFilename = client->m_sResultPath +"/"+ item->getFilename() + "_r.mlf";
                            std::ofstream mlfRFile(mlfFilename, std::ios::out | std::ios::app);
			                if (mlfRFile.is_open()) {
                                mlfRFile << rx;
                                mlfRFile << std::endl;
				                mlfRFile.close();
                            }
                            mlfFilename = client->m_sResultPath +"/"+ item->getFilename() + "_l.mlf";
                            std::ofstream mlfLFile(mlfFilename, std::ios::out | std::ios::app);
			                if (mlfLFile.is_open()) {
                                mlfLFile << tx;
                                mlfLFile << std::endl;
				                mlfLFile.close();
                            }
                        }

                        value= gearman_client_do(gearClient, "vr_text", NULL, 
                                                        (const void*)(rx.c_str()), rx.size(),
                                                        &result_size, &rc);
                        if (gearman_success(rc)) {
                            sValue = (const char*)value;
                            if (sValue[0] == 'E') {
                                err_code = sValue.substr(0, sValue.find("\n"));
                                svr_name = sValue.substr(sValue.find("\n")+1);
                                logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text_rx). [%s : %s], ERROR-CODE(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), err_code.c_str());
                                DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), err_code.c_str(), svr_name.c_str());
                                free(value);
                            }
                            else {
                                nPos1 = 0;
                                nPos2 = 0;
                                
                                nPos2 = sValue.find("\n", nPos1) + 1;    // RES_CODE
                                nPos1 = nPos2;
                                nPos2 = sValue.find("\n", nPos1) + 1;
                                svr_name = sValue.substr(nPos1, nPos2-nPos1-1);   // SERVER_NAME

                                if (value) {
                                    rx_unseg = sValue.substr(nPos2);//(const char*)value;
                                    free(value);
                                }

                                value= gearman_client_do(gearClient, "vr_text", NULL, 
                                                                (const void*)(tx.c_str()), tx.size(),
                                                                &result_size, &rc);
                                if (gearman_success(rc)) {
                                    sValue = (const char*)value;
                                    if (sValue[0] == 'E') {
                                        err_code = sValue.substr(0, sValue.find("\n"));
                                        svr_name = sValue.substr(sValue.find("\n")+1);
                                        logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text_tx). [%s : %s], ERROR-CODE(%s), server_name(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), err_code.c_str(), svr_name.c_str());
                                        DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), err_code.c_str(), svr_name.c_str());
                                        free(value);
                                    }
                                    else {
                                        nPos1 = 0;
                                        nPos2 = 0;
                                        
                                        nPos2 = sValue.find("\n", nPos1) + 1;    // RES_CODE
                                        nPos1 = nPos2;
                                        nPos2 = sValue.find("\n", nPos1) + 1;
                                        svr_name = sValue.substr(nPos1, nPos2-nPos1-1);   // SERVER_NAME

                                        if (value) {
                                            tx_unseg = sValue.substr(nPos2);//(const char*)value;
                                            free(value);
                                        }

                                        {
                                            uint32_t diaNumber=0;
                                            uint8_t spkno=1;
                                            std::istringstream iss(rx_unseg);
                                            std::vector<std::string> strs;

                                            rapidjson::Document d;
                                            rapidjson::Document::AllocatorType& alloc = d.GetAllocator();
                                            int idx=1;

                                            d.SetArray();

                                            while(std::getline(iss, line)) {
                                                boost::split(strs, line, boost::is_any_of(","));
                                                //std::cout << "[1] : " << strs[0] << " [2] : " << strs[1] << " [3] : " << strs[2] << std::endl;
#ifdef CHANGE_STT_DATA
                                                // to DB
                                                if (DBHandler) {
                                                    diaNumber++;
                                                    DBHandler->insertSTTData(diaNumber, item->getCallId(), spkno, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4), strs[2]);
                                                }

                                        d.SetObject();
                                        d.AddMember("IDX", diaNumber, alloc);
                                        // d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                        d.AddMember("SPK", rapidjson::Value((item->spkNo==1)?"R":"L", alloc).Move(), alloc);
                                        d.AddMember("POS_START", sframe[item->spkNo -1]/10, alloc);
                                        d.AddMember("POS_END", eframe[item->spkNo -1]/10, alloc);
                                        d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);
#endif
                                                rapidjson::Value o(rapidjson::kObjectType);
                                                o.AddMember("IDX", idx, alloc);
                                                // o.AddMember("CALL_ID", rapidjson::Value(item->getCallId().c_str(), alloc).Move(), alloc);
                                                o.AddMember("SPK", "R", alloc);
                                                o.AddMember("POS_START", std::stoi(strs[0].c_str()+4), alloc);
                                                o.AddMember("POS_END", std::stoi(strs[1].c_str()+4), alloc);
                                                o.AddMember("VALUE", rapidjson::Value(strs[2].c_str(), alloc).Move(), alloc);

                                                d.PushBack(o, alloc);
                                                idx++;

                                                // to STTDeliver(file), FullText
                                                if (FileHandler) {
                                                    //FileHandler->insertSTT(item->getCallId(), strs[2], 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4));
                                                    FileHandler->insertSTT(item->getCallId(), strs[2], item->getCallId()+"_r");
                                                }
                                            }

                                            rapidjson::StringBuffer strbuf;
                                            rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                            d.Accept(writer);
                                            // to DB
                                            if (DBHandler) {
                                                std::string sttValue = strbuf.GetString();
                                                DBHandler->insertSTTData(0, item->getCallId(), spkno, 0, 0, sttValue);
                                            }

                                        }

                                        {
                                            uint32_t diaNumber=0;
                                            uint8_t spkno=2;
                                            std::istringstream iss(tx_unseg);
                                            std::vector<std::string> strs;

                                            rapidjson::Document d;
                                            rapidjson::Document::AllocatorType& alloc = d.GetAllocator();
                                            int idx=1;

                                            d.SetArray();

                                            while(std::getline(iss, line)) {
                                                boost::split(strs, line, boost::is_any_of(","));
                                                //std::cout << "[1] : " << strs[0] << " [2] : " << strs[1] << " [3] : " << strs[2] << std::endl;
#ifdef CHANGE_STT_DATA
                                                // to DB
                                                if (DBHandler) {
                                                    diaNumber++;
                                                    DBHandler->insertSTTData(diaNumber, item->getCallId(), spkno, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4), strs[2]);
                                                }
#endif // CHANGE_STT_DATA
                                                rapidjson::Value o(rapidjson::kObjectType);
                                                o.AddMember("IDX", idx, alloc);
                                                // o.AddMember("CALL_ID", rapidjson::Value(item->getCallId().c_str(), alloc).Move(), alloc);
                                                o.AddMember("SPK", "L", alloc);
                                                o.AddMember("POS_START", std::stoi(strs[0].c_str()+4), alloc);
                                                o.AddMember("POS_END", std::stoi(strs[1].c_str()+4), alloc);
                                                o.AddMember("VALUE", rapidjson::Value(strs[2].c_str(), alloc).Move(), alloc);

                                                d.PushBack(o, alloc);
                                                idx++;

                                                // to STTDeliver(file), FullText
                                                if (FileHandler) {
                                                    //FileHandler->insertSTT(item->getCallId(), strs[2], 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4));
                                                    FileHandler->insertSTT(item->getCallId(), strs[2], item->getCallId()+"_l");
                                                }
                                            }

                                            rapidjson::StringBuffer strbuf;
                                            rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                            d.Accept(writer);
                                            // to DB
                                            if (DBHandler) {
                                                std::string sttValue = strbuf.GetString();
                                                DBHandler->insertSTTData(0, item->getCallId(), spkno, 0, 0, sttValue);
                                            }

                                        }
                                        auto t2 = std::chrono::high_resolution_clock::now();
                                        logger->debug("VFClient::thrdFunc(%ld) - STT SUCCESS [%s : %s], timeout(%d), fsize(%d), server_name(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout, nFilesize, svr_name.c_str());
                                        DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'Y', nFilesize, nFilesize/16000, std::chrono::duration_cast<std::chrono::seconds>(t2-t1).count(), item->m_procNo, item->getTableName().c_str(), "", svr_name.c_str());
                                    }
                                }
                                else {
                                    // FAIL
                                    logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text_tx). [%s : %s], timeout(%d)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout);
                                    DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), "E20400"/*gearman_client_error(gearClient)*/);
                                }
                            }
                        }
                        else {
                            // FAIL
                            logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text_rx). [%s : %s], timeout(%d)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout);
                            DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), "E20400"/*gearman_client_error(gearClient)*/);
                        }

                    }
                    else {
                        // # Parse Header
                        std::string fsize;
                        nPos1 = 0;
                        nPos2 = 0;

                        if (sValue.find("spk_flag") != string::npos) {
                            // 2. cond.(화자분리), 필요한 인자값 수집 - gearman function 이름값 가져오기

                            nPos1 = sValue.find("spk_node");
                            nPos2 = sValue.find("\"", nPos1) + 1;
                            nPos1 = sValue.find("\"", nPos2);
                            sFuncName = sValue.substr(nPos2, nPos1-nPos2);
                            nPos2 = sValue.find("}\n") + 2;
                            nPos1 = nPos2;
                            nPos2 = sValue.find("\n", nPos1) + 1;    // RES_CODE
                            nPos1 = nPos2;
                            nPos2 = sValue.find("\n", nPos1) + 1;
                            svr_name = sValue.substr(nPos1, nPos2-nPos1-1);   // SERVER_NAME

                            nPos1 = nPos2;
                            nPos2 = sValue.find("\n", nPos1) + 1;
                            nFilesize = std::stoi(sValue.substr(nPos1, nPos2 - nPos1 -1));
                            nPos1 = nPos2;
                            //sValue.find("\n");
                            //value = (void *)(sValue.c_str() + nPos1);
                            //result_size = strlen((const char*)value);

                        }
                        else {
                            nPos2 = sValue.find("\n", nPos1) + 1;    // RES_CODE
                            nPos1 = nPos2;
                            nPos2 = sValue.find("\n", nPos1) + 1;
                            svr_name = sValue.substr(nPos1, nPos2-nPos1-1);   // SERVER_NAME

                            nPos1 = nPos2;
                            nPos2 = sValue.find("\n", nPos1) + 1;
                            nFilesize = std::stoi(sValue.substr(nPos1, nPos2 - nPos1 -1));
                            nPos1 = nPos2;//sValue.find("\n");
                        }

                        // Save MLF(Optional)
                        if (client->m_bMakeMLF) {
                            // sValue offset nPos1
                            std::string mlfFilename;
                            // save MLF to MLF-File
                            // rx, tx
                            mlfFilename = client->m_sResultPath +"/"+ item->getFilename() + ".mlf";
                            std::ofstream mlfFile(mlfFilename, std::ios::out | std::ios::app);
			                if (mlfFile.is_open()) {
                                mlfFile << sValue.c_str() + nPos1;
                                mlfFile << std::endl;
				                mlfFile.close();
                            }
                        }

                        // 2. Unsegment! : JOB_UNSEGMENT
                        value= gearman_client_do(gearClient, "vr_text", NULL, 
                                                        (const void*)(sValue.c_str() + nPos1), strlen(sValue.c_str() + nPos1),
                                                        &result_size, &rc);
                        if (gearman_success(rc)) {
                            sValue = (const char*)value;
                            if (sValue[0] == 'E') {
                                err_code = sValue.substr(0, sValue.find("\n"));
                                svr_name = sValue.substr(sValue.find("\n")+1);
                                logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text). [%s : %s], ERROR-CODE(%s), server_name(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), err_code.c_str(), svr_name.c_str());
                                DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), err_code.c_str(), svr_name.c_str());
                                free(value);
                            }
                            else {
                                nPos1 = 0;
                                nPos2 = 0;

                                nPos2 = sValue.find("\n", nPos1) + 1;    // RES_CODE
                                nPos1 = nPos2;
                                nPos2 = sValue.find("\n", nPos1) + 1;
                                svr_name = sValue.substr(nPos1, nPos2-nPos1-1);   // SERVER_NAME
                                // Make use of value
                                if (value) {
                                    uint32_t diaNumber=0;
                                    uint8_t spkno=0;
                                    std::string strValue(sValue.substr(nPos2));//((const char*)value);

                                    free(value);

                                    //std::cout << "STT RESULT <<\n" << (const char*)value << "\n>>" << std::endl;
        #ifdef CODE_EXAM_SECTION
                                    while(std::getline(iss, line)) {
                                        boost::split(strs, line, boost::is_any_of(","));
                                        //std::cout << "[1] : " << strs[0] << " [2] : " << strs[1] << " [3] : " << strs[2] << std::endl;

                                        // to DB
                                        if (DBHandler) {
                                            diaNumber++;
                                            DBHandler->insertSTTData(diaNumber, item->getCallId(), 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4), strs[2]);
                                        }

                                        // to STTDeliver(file), FullText
                                        if (FileHandler) {
                                            //FileHandler->insertSTT(item->getCallId(), strs[2], 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4));
                                            FileHandler->insertSTT(item->getCallId(), strs[2], item->getCallId());
                                        }
                                    }
        #endif
                                    // Unsegment 결과를 정제(parsing)하여 목적에 따라 처리한다.

                                    // # 화자 분리
                                    if (item->getRxTxType().compare("RX") == 0){
                                        spkno=1;
                                    }
                                    else if (item->getRxTxType().compare("TX") == 0){
                                        spkno=2;
                                    }

                                    {
                                        std::istringstream iss(strValue);
                                        std::vector<std::string> strs;

                                        rapidjson::Document d;
                                        rapidjson::Document::AllocatorType& alloc = d.GetAllocator();
                                        int idx=1;

                                        d.SetArray();

                                        while(std::getline(iss, line)) {
                                            char spk[3];
                                            boost::split(strs, line, boost::is_any_of(","));
                                            //std::cout << "[1] : " << strs[0] << " [2] : " << strs[1] << " [3] : " << strs[2] << std::endl;
#ifdef CHANGE_STT_DATA
                                            // to DB
                                            if (DBHandler) {
                                                diaNumber++;
                                                DBHandler->insertSTTData(diaNumber, item->getCallId(), spkno, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4), strs[2]);
                                            }
#endif
                                            switch(spkno) {
                                                case 1:
                                                sprintf(spk, "%c", 'R');
                                                break;
                                                case 2:
                                                sprintf(spk, "%c", 'L');
                                                break;
                                                default:
                                                sprintf(spk, "%c", 'N');
                                            }
                                            rapidjson::Value o(rapidjson::kObjectType);
                                            o.AddMember("IDX", idx, alloc);
                                            // o.AddMember("CALL_ID", rapidjson::Value(item->getCallId().c_str(), alloc).Move(), alloc);
                                            o.AddMember("SPK", rapidjson::Value(spk, alloc).Move(), alloc);
                                            o.AddMember("POS_START", std::stoi(strs[0].c_str()+4), alloc);
                                            o.AddMember("POS_END", std::stoi(strs[1].c_str()+4), alloc);
                                            o.AddMember("VALUE", rapidjson::Value(strs[2].c_str(), alloc).Move(), alloc);

                                            d.PushBack(o, alloc);
                                            idx++;

                                            // to STTDeliver(file), FullText
                                            if (FileHandler) {
                                                //FileHandler->insertSTT(item->getCallId(), strs[2], 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4));
                                                FileHandler->insertSTT(item->getCallId(), strs[2], item->getCallId());
                                            }
                                        }

                                        rapidjson::StringBuffer strbuf;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                        d.Accept(writer);
                                        // to DB
                                        if (DBHandler) {
                                            std::string sttValue = strbuf.GetString();
                                            DBHandler->insertSTTData(0, item->getCallId(), spkno, 0, 0, sttValue);
                                        }

                                    }

                                    if (sFuncName.size()) {
                                        VASDivSpeaker divspk(DBHandler, FileHandler, item);

                                        //startWork(gearman_client_st *gearClient, std::string &funcname, std::string &unseg);
                                        divspk.startWork(gearClient, sFuncName, strValue);
                                    }

                                    // DBHandler에서 처리할 수 있도록... VRClient와 동일하게?
                                    // 그럼... 전체 STT결과 처리는?

                                }
                                auto t2 = std::chrono::high_resolution_clock::now();
                                logger->debug("VFClient::thrdFunc(%ld) - STT SUCCESS [%s : %s], timeout(%d), fsize(%d), server_name(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout, nFilesize, svr_name.c_str());
                                DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'Y', nFilesize, nFilesize/16000, std::chrono::duration_cast<std::chrono::seconds>(t2-t1).count(), item->m_procNo, item->getTableName().c_str(), "", svr_name.c_str());
                            }
                        }
                        else if (gearman_failed(rc)) {
                            logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_text). [%s : %s], timeout(%d)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout);
                            DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), "E20400"/*gearman_client_error(gearClient)*/);
                        }
                    }
#endif  // USE_RAPIDJSON
                }
                else {
                    logger->info("VFClient::thrdFunc(%ld) - Success to get gearman(vr_stt) but empty result.  [%s : %s], server_name(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), svr_name.c_str());
                    DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'Y', 0, 0, 0, item->m_procNo, item->getTableName().c_str());
                }
            }
            else {
                logger->error("VFClient::thrdFunc(%ld) - failed gearman_client_do(vr_stt). [%s : %s], timeout(%d), gearman_error_msg(%s)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout, gearman_client_error(gearClient));
                DBHandler->updateTaskInfo(item->getCallId(), item->m_regdate, item->getRxTxType(), item->getCounselorCode(), 'X', 0, 0, 0, item->m_procNo, item->getTableName().c_str(), "E20400"/*gearman_client_error(gearClient)*/);
            }
#else
            // 1. Start STT : JOB_STT
            if (client->requestGearman(gearClient, "vr_stt", reqFilePath.c_str(), reqFilePath.size(), sValue)) {

                // 2Ch Wave
                if ( sValue.find("||") != std::string::npos ) {
                    std::string rx(sValue.substr(0, sValue.find("||")));
                    std::string tx(sValue.substr(sValue.find("||")+2));

                    // loop 2 times

                }
                else {
                    std::string sFuncName="";
                    size_t nPos1=0, nPos2=0;
                    int nFilesize=0;

                    // Software Speaker divide
                    if ( sValue.find("spk_flag") != std::string::npos ) {
                        nPos1 = sValue.find("spk_node");
                        nPos2 = sValue.find("'", nPos1) + 1;
                        nPos1 = sValue.find("'", nPos2);
                        sFuncName = sValue.substr(nPos2, nPos1-nPos2);
                        nPos2 = sValue.find("\n") + 1;
                        nFilesize = std::stoi(sValue.c_str() + nPos2);

                    }

                    if (client->requestGearman(gearClient, "vr_text", (sValue.c_str() + nPos1), strlen(sValue.c_str() + nPos1), sValue)) {
                        if (item->getRxTxType()){
                            // 
                            DivSpkManager *pDSM = DivSpkManager::instance();

                            pDSM->doDivSpeaker(item->getCallId(), sValue);
                        }
                        else {
                            uint32_t diaNumber=0;
                            std::istringstream iss(sValue);
                            std::vector<std::string> strs;
                            while(std::getline(iss, line)) {
                                boost::split(strs, line, boost::is_any_of(","));
                                //std::cout << "[1] : " << strs[0] << " [2] : " << strs[1] << " [3] : " << strs[2] << std::endl;

                                // to DB
                                if (DBHandler) {
                                    diaNumber++;
                                    DBHandler->insertSTTData(diaNumber, item->getCallId(), 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4), strs[2]);
                                }

                                // to STTDeliver(file), FullText
                                if (FileHandler) {
                                    //FileHandler->insertSTT(item->getCallId(), strs[2], 0, std::stoi(strs[0].c_str()+4), std::stoi(strs[1].c_str()+4));
                                    FileHandler->insertSTT(item->getCallId(), strs[2], item->getCallId());
                                }
                            }
                        }

                        if (sFuncName.size()) {
                            VASDivSpeaker divspk(DBHandler, FileHandler, item);

                            //startWork(gearman_client_st *gearClient, std::string &funcname, std::string &unseg);
                            divspk.startWork(gearClient, sFuncName, sValue);
                        }

                    }
                    else {
                        logger->error("VFClient::thrdFunc(%ld) - failed requestGearman(vr_text). [%s : %s], timeout(%d)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout);
                        DBHandler->updateTaskInfo(item->getCallId(), item->getRxTxType(), item->getCounselorCode(), 'X', item->getTableName().c_str(), gearman_client_error(gearClient));
                    }
                }

            }
            else {
                logger->error("VFClient::thrdFunc(%ld) - failed requestGearman(vr_stt). [%s : %s], timeout(%d)", client->m_nNumId, item->getCallId().c_str(), item->getFilename().c_str(), client->m_nGearTimeout);
                DBHandler->updateTaskInfo(item->getCallId(), item->getRxTxType(), item->getCounselorCode(), 'X', item->getTableName().c_str(), gearman_client_error(gearClient));
            }
#endif
            delete item;
            item = nullptr;
        }
        else {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        
        }
        //logger->debug("VFClient::thrdFunc() working...");
    }
#if 1
    gearman_client_free(gearClient);
#endif
}

bool VFClient::requestGearman(gearman_client_st *gearClient, const char* funcname, const char*reqValue, size_t reqLen, std::string &resStr)
{
    bool ret = false;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;

    value= gearman_client_do(gearClient, funcname, NULL, 
                                    (const void*)reqValue, reqLen,
                                    &result_size, &rc);
    if (gearman_success(rc)) {
        resStr = (const char*)value;

        free(value);
        ret = true;
    }
    else {
        resStr = gearman_client_error(gearClient);
    }

    return ret;
}
