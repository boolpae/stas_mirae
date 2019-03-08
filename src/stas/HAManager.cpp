/*
*   HAManager Class
*   desc: 이중화 모듈
*   creator: boolpae
*/

#include "HAManager.h"

#ifdef ENABLE_REALTIME
#include "VRCManager.h"
#include "VDCManager.h"
#endif

#include "stas.h"

#include <chrono>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>


HAManager* HAManager::m_instance= nullptr;

#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
HAManager::HAManager(VRCManager *vrm, VDCManager *vdm)
: m_vrm(vrm), m_vdm(vdm), m_nActiveSock(0), m_nStandbySock(0)
#else
HAManager::HAManager()
: m_nActiveSock(0), m_nStandbySock(0)
#endif
#else
HAManager::HAManager()
: m_nActiveSock(0), m_nStandbySock(0)
#endif
{
#ifdef LOG4CPP
    m_Logger = config->getLogger();
#endif
    m_mSyncTable.clear();
    m_bLiveFlag = true;
}

HAManager::~HAManager()
{
    std::map< std::string, SyncItem* >::iterator iter;

    m_bLiveFlag = false;
    
    for(iter = m_mSyncTable.begin(); iter != m_mSyncTable.end(); iter++) {
        delete iter->second;
    }
    m_mSyncTable.clear();

    while(!m_qSignalQue.empty()) {
        delete m_qSignalQue.front();
        m_qSignalQue.pop();
    }

}

#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
HAManager* HAManager::instance(VRCManager *vrm, VDCManager *vdm)
#else
HAManager* HAManager::instance()
#endif
#else
HAManager* HAManager::instance()
#endif
{
    if (m_instance) return m_instance;
#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
    m_instance = new HAManager(vrm, vdm);
#else
    m_instance = new HAManager();
#endif
#else
    m_instance = new HAManager();
#endif

    return m_instance;
}

HAManager* HAManager::getInstance()
{
    return m_instance;
}

void HAManager::release()
{
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

int HAManager::init(std::string ipaddr, uint16_t port)
{
    // 0. Standby 상태로 시작
    // 1. Active 서버로 접속 시도
    // 1-1. 접속이 연결된 경우 : Standby 상태로 수행(Active로부터 CallSignal 정보 받아 처리)
    // 1-2. 접속이 끊어진 경우 : Standby 상태에서 Active 상태로 변경 및 동기화 서버 수행 시작
    // 2. Active 서버로 접속이 실패한 경우 Standby -> Active로 상태 변경
    // 3. 동기화 서버 수행 시작
	struct sockaddr_in addr;
    std::thread thrd;
	if ((m_nStandbySock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("HAManager::init() :");
#ifdef LOG4CPP
        if (m_Logger) m_Logger->error("HAManager::init() - failed get socket : %d", errno);
#endif
		return -1;
	}

    m_sAddr = ipaddr;
    m_nPort = port;
    m_bStat = false;

	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(m_sAddr.c_str());
	addr.sin_port = htons(m_nPort);

    if (connect(m_nStandbySock, (struct sockaddr*)&addr, sizeof(addr))) {
        // Active로 연결 시도 실패한 경우 Standby에서 Active로 변경 및 동기화 서버 수행
        close(m_nStandbySock);
        m_nStandbySock = 0;
        m_bStat = true;
        thrd = std::thread(HAManager::thrdActive, this);
        thrd.detach();
#ifdef LOG4CPP
        if (m_Logger) m_Logger->info("HAManager::init() - Start Active Mode");
#endif
    }
    else {
        thrd = std::thread(HAManager::thrdStandby, this, m_nStandbySock);
        thrd.detach();
#ifdef LOG4CPP
        if (m_Logger) m_Logger->info("HAManager::init() - Start Standby Mode");
#endif
    }

    // Server Socket

    return 0;
}

// Standby 상태에서 동작하는 쓰레드 함수
// Active에서 보내주는 시그널 정보들을 수신/관리한다.
void HAManager::thrdStandby(HAManager *mgr, int standbysock)
{
    char buff[256];
    uint16_t nBodyLen;
    struct timeval tv;
    fd_set rfds;
    int selVal;
    int msg_size;
    std::string sRecvString;

    memset(buff, 0, sizeof(buff));
    memcpy(buff, "STAS", 4);
    nBodyLen = htons(4);
    memcpy(buff+4, &nBodyLen, sizeof(uint16_t));
    memcpy(buff+4+sizeof(uint16_t), "INIT", 4);
    write(standbysock, buff, 10);

    while( mgr->m_bLiveFlag ) {
		tv.tv_sec = 1;	// for debug
		tv.tv_usec = 0; // 500000;
		FD_ZERO(&rfds);
		FD_SET(standbysock, &rfds);

		selVal = select(standbysock+1, &rfds, NULL, NULL, &tv);

        if (selVal > 0) {
            msg_size = read(standbysock, buff, 4+sizeof(uint16_t));

            // 0. 연결이 끊어질 경우 상태를 Active로 변경한 후 동기화 서버 시작
            if (msg_size == 0) {
                printf("Standby : Disconnected from Active\n");
#ifdef LOG4CPP
                if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdStandby() - Disconnected from Active, change to Active from Standby");
#endif
                close(standbysock);
                break;
            }

            // 1. Standby 상태이며 Active로부터 실시간 CallSignal 수신 및 처리
            if ( memcmp(buff, "STAS", 4) ) {
                while( sizeof(buff) > (size_t)read(standbysock, buff, sizeof(buff)));
            }
            else {
                int nSignalCount=0;
                char cSigType;
                char sCallId[129];
                char sCounselCode[33];
                char sFuncName[33];
                uint16_t port1;
                uint16_t port2;
#ifdef EN_RINGBACK_LEN
                uint32_t ringbacklen;
#endif
                memcpy(&nBodyLen, buff+4, sizeof(uint16_t));
                nBodyLen = ntohs(nBodyLen);

#ifdef EN_RINGBACK_LEN
                nSignalCount = nBodyLen/208;
#else
                nSignalCount = nBodyLen/203;
#endif
                sRecvString.clear();
                while(nBodyLen && (msg_size = read(standbysock, buff, sizeof(buff)-1))) {
                    buff[msg_size] = 0;
                    sRecvString += std::string(buff);
                    nBodyLen -= msg_size;
                }

                if (nSignalCount > 0) {
                    for(int i=0; i<nSignalCount; i++) {
                        memset(buff, 0, sizeof(buff));
#ifdef EN_RINGBACK_LEN
                        memcpy(buff, sRecvString.c_str() + (i * 208), 208);
                        sscanf(buff, "%c%128s%32s%32s%5hd%5hd%5d", &cSigType, sCallId, sCounselCode, sFuncName, &port1, &port2, &ringbacklen);
#else
                        memcpy(buff, sRecvString.c_str() + (i * 203), 203);
                        sscanf(buff, "%c%128s%32s%32s%5hd%5hd", &cSigType, sCallId, sCounselCode, sFuncName, &port1, &port2);
#endif
                        if (cSigType == 'A') { // Add SignalItem
#ifdef EN_RINGBACK_LEN
                            SyncItem *item = new SyncItem(cSigType, std::string(sCallId), std::string(sCounselCode), std::string(sFuncName), port1, port2, ringbacklen);
#else
                            SyncItem *item = new SyncItem(cSigType, std::string(sCallId), std::string(sCounselCode), std::string(sFuncName), port1, port2);
#endif
                            mgr->m_mSyncTable[std::string(sCallId)] = item;

#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
                            // control channel logic
                            if (mgr->m_vrm) {
                                // set vrc worker info
#ifdef EN_RINGBACK_LEN
                                if (mgr->m_vrm->addVRC(std::string(sCallId), std::string(sCounselCode), std::string(sFuncName), 'R', 2, ringbacklen))
#else
                                if (mgr->m_vrm->addVRC(std::string(sCallId), std::string(sCounselCode), std::string(sFuncName), 'R', 2))
#endif
                                {
                                    if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdStandby() - Failed Set VRC : %s", sCallId);
                                }
                                else {
                                    if (mgr->m_vdm) {
                                        // set vdc channel info
                                        if (mgr->m_vdm->setActiveVDC(std::string(sCallId), 1, port1)
                                            || mgr->m_vdm->setActiveVDC(std::string(sCallId), 2, port2)) {
                                            mgr->m_vdm->removeVDC(std::string(sCallId));
                                            mgr->m_vrm->removeVRC(std::string(sCallId));
                                            if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdStandby() - Failed Set VDC : %s", sCallId);
                                        }
                                    }
                                }
                            }
#endif
#endif
                            std::cout << "Add CallSignal : " << sCallId << std::endl;
#ifdef LOG4CPP
                            if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdStandby() - Add CallSignal : %s", sCallId);
#endif
                        }
                        else {  // Remove SignalItem
                            SyncItem *item = mgr->m_mSyncTable[std::string(sCallId)];
                            if (item) delete item;
                            mgr->m_mSyncTable.erase(std::string(sCallId));

#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
                            // control channel logic
                            if (mgr->m_vrm) {
                                // set vrc worker info
                                mgr->m_vrm->removeVRC(std::string(sCallId));
                            }
                            if (mgr->m_vdm) {
                                // set vdc channel info
                                mgr->m_vdm->removeVDC(std::string(sCallId));
                            }
#endif
#endif
                            std::cout << "Remove CallSignal : " << sCallId << std::endl;
#ifdef LOG4CPP
                            if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdStandby() - Remove CallSignal : %s", sCallId);
#endif
                        }
                        //mgr->outputSignals();
                    }
                }
            }
        }
    }

    mgr->m_nStandbySock = 0;
    if (mgr->m_bLiveFlag) {
        std::thread thrd;
        mgr->m_bStat = true;
#ifdef ENABLE_REALTIME
        thrd = std::thread(HAManager::thrdActive, HAManager::instance(mgr->m_vrm, mgr->m_vdm/*, mgr->m_Logger*/));
#else
        thrd = std::thread(HAManager::thrdActive, HAManager::instance());
#endif
        thrd.detach();
    }
}

// Active 상태에서 동작하는 쓰레드 함수
// 만약 Standby에 연결되지 않았다면 아무 동작도 하지 않는다.
void HAManager::thrdActive(HAManager *mgr)
{
    std::thread thrSender;
    // 서버 소켓 생성
	struct sockaddr_in addr;
    struct sockaddr_in client_addr;
    int enable = 1;
    
	if ((mgr->m_nActiveSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("HAManager::thrdSender() :");
#ifdef LOG4CPP
        if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdActive() - failed get socket : %d", errno);
#endif
		return ;
	}

    if (setsockopt(mgr->m_nActiveSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("HAManager::thrdSender() - setsockopt() :");
#ifdef LOG4CPP
        if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdActive() - failed get socket : %d", errno);
#endif
		return ;
	}

	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(mgr->m_nPort);

    if(bind(mgr->m_nActiveSock, (struct sockaddr *)&addr, sizeof(addr)) <0)
    {//bind() 호출
        printf("Server : Can't bind local address.\n");
#ifdef LOG4CPP
        if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdActive() - Server : Can't bind local address.");
#endif
        close(mgr->m_nActiveSock);
        return ;
    }
 
    if(listen(mgr->m_nActiveSock, 1) < 0)
    {//소켓을 수동 대기모드로 설정
        printf("Server : Can't listening connect.\n");
#ifdef LOG4CPP
        if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdActive() - Server : Can't listening connect.");
#endif
        close(mgr->m_nActiveSock);
        return ;
    }

    while( mgr->m_bLiveFlag ) {
        // 0. Server 소켓 생성 후 listen
        // 1. Standby에서 접속 요청 및 Active에서 accept
        char buff[256];
        uint16_t nBodyLen;
        struct timeval tv;
        fd_set rfds;
        int selVal;
        int len = sizeof(client_addr);
        int client_fd;

		tv.tv_sec = 1;	// for debug
		tv.tv_usec = 0; // 500000;
		FD_ZERO(&rfds);
		FD_SET(mgr->m_nActiveSock, &rfds);

		selVal = select(mgr->m_nActiveSock+1, &rfds, NULL, NULL, &tv);

        if (selVal > 0) {
            client_fd = accept(mgr->m_nActiveSock, (struct sockaddr *)&client_addr, (socklen_t*)&len);
            inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, buff, sizeof(buff));
            printf("Server : %s client connected.\n", buff);
#ifdef LOG4CPP
            if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdActive() - Server : %s client connected.", buff);
#endif

            if(client_fd < 0)
            {
                printf("Server: accept failed.\n");
#ifdef LOG4CPP
                if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdActive() - Server: accept failed.");
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            tv.tv_sec = 1;	// for debug
            tv.tv_usec = 0; // 500000;
            FD_ZERO(&rfds);
            FD_SET(client_fd, &rfds);

            selVal = select(client_fd+1, &rfds, NULL, NULL, &tv);

            if (selVal > 0) {
                // 2. 접속 후 최초엔 현재 MapTable에 있는 CallSignal 정보들을 전송
                if (read(client_fd, buff, 4+sizeof(uint16_t)) <= 0) {
                    close(client_fd);
                    printf("Server: Disconnected(1st read) from Client\n");
#ifdef LOG4CPP
                    if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdActive() - Server: Disconnected(1st read) from Client");
#endif
                    break;
                }
                memcpy(&nBodyLen, buff+4, sizeof(uint16_t));
                if (memcmp(buff, "STAS", 4)) {
                    close(client_fd);
                    printf("Server: Invalid packet from Standby.\n");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
#ifdef LOG4CPP
                    if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdActive() - Server: Invalid packet from Standby.");
#endif
                    continue;
                }
                nBodyLen = ntohs(nBodyLen);
                if ((len=read(client_fd, buff, sizeof(buff))) <= 0) {
                    close(client_fd);
                    printf("Server: Disconnected(2nd read) from Client\n");
#ifdef LOG4CPP
                    if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdActive() - Server: Disconnected(2nd read) from Client");
#endif
                    break;
                }

                // send callsignal infos in map
                /* 이 곳에 Active 서버가 가지고 있는 CallSignal 정보 모두를 보낸다.
                *
                */
                std::map< std::string, SyncItem* >::iterator iter;
                SyncItem *syncItem;

                memset(buff, 0, sizeof(buff));
                memcpy(buff, "STAS", 4);
#ifdef EN_RINGBACK_LEN
                nBodyLen = htons(mgr->m_mSyncTable.size() * 208);
#else
                nBodyLen = htons(mgr->m_mSyncTable.size() * 203);
#endif
                memcpy(buff+4, &nBodyLen, sizeof(uint16_t));

                len = -1;   // initialize
                if ( mgr->m_mSyncTable.size() && ((len=write(client_fd, buff, 4+sizeof(uint16_t))) > 0) ) {
                    for(iter = mgr->m_mSyncTable.begin(); iter != mgr->m_mSyncTable.end(); iter++) {
                        syncItem = iter->second;
#ifdef EN_RINGBACK_LEN
                        sprintf(buff, "%c%-128s%-32s%-32s%-5hd%-5hd%-5d", (syncItem->m_bSignalType?'A':'R'),syncItem->m_sCallId.c_str(),syncItem->m_sCounselCode.c_str(), syncItem->m_sFuncName.c_str(), syncItem->m_n1port, syncItem->m_n2port, syncItem->m_nRingbackLen);
                        if (write(client_fd, buff, 208) <= 0) break;
#else
                        sprintf(buff, "%c%-128s%-32s%-32s%-5hd%-5hd", (syncItem->m_bSignalType?'A':'R'),syncItem->m_sCallId.c_str(),syncItem->m_sCounselCode.c_str(), syncItem->m_sFuncName.c_str(), syncItem->m_n1port, syncItem->m_n2port);
                        if (write(client_fd, buff, 203) <= 0) break;
#endif
                    }

                    if (iter != mgr->m_mSyncTable.end()) {
                        close(client_fd);
                        printf("Server: Disconnected(write error) from Client\n");
#ifdef LOG4CPP
                        if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdSender() - Server: Disconnected(write error) from Client");
#endif
                        break;
                    }
                }
                else if ( mgr->m_mSyncTable.size() && (len<0) ) {
                    close(client_fd);
                    printf("Server: Disconnected(write) from Client\n");
#ifdef LOG4CPP
                    if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdSender() - Server: Disconnected(write) from Client");
#endif
                    break;
                }

                // 3. 이 후 실시간 CallSignal 정보를 전송
                mgr->m_bSenderFlag = true;
                thrSender = std::thread(HAManager::thrdSender, mgr, client_fd);

                while (mgr->m_bLiveFlag) {
                    tv.tv_sec = 1;	// for debug
                    tv.tv_usec = 0; // 500000;
                    FD_ZERO(&rfds);
                    FD_SET(client_fd, &rfds);

                    selVal = select(client_fd+1, &rfds, NULL, NULL, &tv);

                    if (selVal > 0) {
                        if (read(client_fd, buff, sizeof(buff))==0) {
                            close(client_fd);
                            printf("Server: Disconnected from Client\n");
#ifdef LOG4CPP
                            if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdSender() - Server: Disconnected from Client");
#endif
                            break;
                        }
                    }

                }

                // thrSender 종료를 위한 Flag 세팅
                mgr->m_bSenderFlag = false;
                if (thrSender.joinable()) thrSender.join();
            }
            else {
                close(client_fd);
                printf("Server: select timeover from Standby.\n");
#ifdef LOG4CPP
                if (mgr->m_Logger) mgr->m_Logger->error("HAManager::thrdSender() - Server: select timeover from Standby.");
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

}

// thrdActive()에서 생성된 쓰레드 함수
// 모든 CallSignal에 대해서 연결된 Standby로 보내는 역할을 담당
void HAManager::thrdSender(HAManager *mgr, int sockfd)
{
    SyncItem *item;
	std::lock_guard<std::mutex> *g;// (m_mxQue);
    char buff[256];
    uint16_t nLen;

    while( mgr->m_bSenderFlag ) {
        // 1. Active 상태이며 Standby로 실시간 CallSignal 송신
        // 2. 연결이 끊어질 경우 상태를 Active로 변경한 후 동기화 서버 시작
        while(!mgr->m_qSignalQue.empty()) {
			g = new std::lock_guard<std::mutex>(mgr->m_mxQue);
            item = mgr->m_qSignalQue.front();
            mgr->m_qSignalQue.pop();
            delete g;

            memset(buff, 0, sizeof(buff));
#ifdef EN_RINGBACK_LEN
            memcpy(buff, "STAS", 4);
            nLen = htons(208);
            memcpy(buff+4, &nLen, sizeof(uint16_t));
            sprintf(buff+6, "%c%-128s%-32s%-32s%-5hd%-5hd%-5d", (item->m_bSignalType?'A':'R'), item->m_sCallId.c_str(), item->m_sCounselCode.c_str(), item->m_sFuncName.c_str(), item->m_n1port, item->m_n2port, item->m_nRingbackLen);
            write(sockfd, buff, 214);
#else
            memcpy(buff, "STAS", 4);
            nLen = htons(203);
            memcpy(buff+4, &nLen, sizeof(uint16_t));
            sprintf(buff+6, "%c%-128s%-32s%-32s%-5hd%-5hd", (item->m_bSignalType?'A':'R'), item->m_sCallId.c_str(), item->m_sCounselCode.c_str(), item->m_sFuncName.c_str(), item->m_n1port, item->m_n2port);
            write(sockfd, buff, 209);
#endif
            if (item->m_bSignalType) {
#ifdef LOG4CPP
                if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdSender() - Add CallSiganl : %s", item->m_sCallId.c_str());
#endif
            }
            else {
#ifdef LOG4CPP
                if (mgr->m_Logger) mgr->m_Logger->info("HAManager::thrdSender() - Remove CallSignal : %s", item->m_sCallId.c_str());
#endif
            }
            delete item;
            //mgr->outputSignals();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

#ifdef EN_RINGBACK_LEN
int HAManager::insertSyncItem( bool calltype, std::string callid, std::string counselcode, std::string funcname, uint16_t port1, uint16_t port2, uint32_t ringbacklen )
#else
int HAManager::insertSyncItem( bool calltype, std::string callid, std::string counselcode, std::string funcname, uint16_t port1, uint16_t port2 )
#endif
{
    SyncItem *item;
    std::lock_guard<std::mutex> g(m_mxQue);

    if (m_bSenderFlag) {
#ifdef EN_RINGBACK_LEN
        item = new SyncItem(calltype, callid, counselcode, funcname, port1, port2, ringbacklen);
#else
        item = new SyncItem(calltype, callid, counselcode, funcname, port1, port2);
#endif
        m_qSignalQue.push(item);
    }

    if (calltype) {
#ifdef EN_RINGBACK_LEN
        item = new SyncItem(calltype, callid, counselcode, funcname, port1, port2, ringbacklen);
#else
        item = new SyncItem(calltype, callid, counselcode, funcname, port1, port2);
#endif
        m_mSyncTable[callid] = item;
    }
    else {
        delete m_mSyncTable[callid];
        m_mSyncTable.erase(callid);
    }

    //outputSignals();
    
    return 0;
}

void HAManager::deleteSyncItem(std::string callid)
{
    if (m_mSyncTable.find(callid) != m_mSyncTable.end()) {
        std::lock_guard<std::mutex> g(m_mxQue);
        delete m_mSyncTable[callid];
        m_mSyncTable.erase(callid);
        
    }
}

void HAManager::outputSignals()
{
    SyncItem *item;
    std::map< std::string, SyncItem* >::iterator iter;// m_mSyncTable

    if(m_mSyncTable.size()) {
        char buff[256];
        std::string output;
        sprintf(buff, "HA Stat(%s), Signal Count : (%lu)\n", m_bStat?"Active":"Standby", m_mSyncTable.size());
        output = buff;
        for(iter = m_mSyncTable.begin(); iter != m_mSyncTable.end(); iter++) {
            item = iter->second;
            sprintf(buff, "\tCall-ID(%s) Funcname(%s) Port1(%d) Port2(%d)\n", item->m_sCallId.c_str(), item->m_sFuncName.c_str(), item->m_n1port, item->m_n2port);
            output += buff;
        }
#ifdef LOG4CPP
        if (m_Logger) m_Logger->info("HAManager::outputSignals() - <<\n%s\n>>", output.c_str());
#endif
    }
}

#ifdef EN_RINGBACK_LEN
SyncItem::SyncItem(bool bSignalType, std::string sCallId, std::string sCounselCode, std::string sFuncName, unsigned short n1port, unsigned short n2port, uint32_t ringbacklen)
    : m_bSignalType(bSignalType), m_sCallId(sCallId), m_sCounselCode(sCounselCode), m_sFuncName(sFuncName), m_n1port(n1port), m_n2port(n2port), m_nRingbackLen(ringbacklen)
#else
SyncItem::SyncItem(bool bSignalType, std::string sCallId, std::string sCounselCode, std::string sFuncName, unsigned short n1port, unsigned short n2port)
    : m_bSignalType(bSignalType), m_sCallId(sCallId), m_sCounselCode(sCounselCode), m_sFuncName(sFuncName), m_n1port(n1port), m_n2port(n2port)
#endif
{

}

SyncItem::~SyncItem() {}

