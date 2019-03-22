#ifdef  ENABLE_REALTIME

#include "VDClient.h"

#ifdef USE_REALTIME_MT
#include "VRClientMT.h"
#else
#include "VRClient.h"
#endif

#include "VRCManager.h"
#include "WorkTracer.h"
#include "HAManager.h"
#include "stas.h"

#include <thread>
#include <iostream>
#include <fstream>

#ifndef WIN32
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>

#include <arpa/inet.h>

#undef htons
#undef ntohs

#else

#include <WS2tcpip.h> // for socklen_t type

#endif

VDClient::VDClient(VRCManager *vrcm)
	: m_nLiveFlag(1), m_nWorkStat(0), m_nPort(0), m_nSockfd(0), m_sCallId(""), m_sCounselCode(""), m_nSpkNo(0), m_vrcm(vrcm), /*m_Logger(logger),*/ m_nPlaytime(3*16000)
{
	m_pVrc = NULL;
	m_tTimeout = time(NULL);

	m_Logger = config->getLogger();
    m_Logger->debug("VDClinet Constructed.");
}

void VDClient::finish()
{
	m_nLiveFlag = 0;

	// 인스턴스 생성 후 init() 실패 후 finish() 호출한 경우, 생성된 인스턴스 삭제
	if (m_nSockfd) {
        close(m_nSockfd);
		m_thrd.detach();//
	}

	delete this;

}

uint16_t VDClient::init(uint16_t port)
{
	struct sockaddr_in addr;

	if ((m_nSockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("VDClient::init() :");
        m_Logger->error("VDClient::init() - failed get socket : %d", errno);
		m_nSockfd = 0;
		return uint16_t(1);	// socket 생성 오류
	}

	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (::bind(m_nSockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("VDClient::init() - bind : ");
        m_Logger->error("VDClient::init() - failed get bind : %d", errno);
		closesocket(m_nSockfd);
		m_nSockfd = 0;
		return uint16_t(2);	// bind 오류
	}

	m_nPort = port;

    m_Logger->info("VDClient::init() - port(%d)", port);

	m_thrd = std::thread(VDClient::thrdMain, this);

	return uint16_t(0);
}

#define BUFLEN 65000  //Max length of buffer
#define VOICE_BUFF_LEN (20000)
#define LEN_OF_VOICE ( 16000 * 5 )
#define SIL_BUFLEN 19200
void VDClient::thrdMain(VDClient * client)
{
	char buf[BUFLEN];
	struct sockaddr_in si_other;
	struct timeval tv;
	fd_set rfds;
	int selVal;
	int recv_len;
	socklen_t slen = sizeof(si_other);
	QueItem* item = NULL;
	char* pos = NULL;
	uint16_t nVDSize;
	char silBuf[SIL_BUFLEN];
	bool bUseSilBuf = false;
	int nTimeout = 30;

	memset(silBuf, 0, SIL_BUFLEN);
	bUseSilBuf = !config->getConfig("stas.send_sil_during_no_voice", "false").compare("true");
	nTimeout = config->getConfig("stas.call_timeout_sec", 30);

	while (client->m_nLiveFlag) {
		//clear the buffer by filling null, it might have previously received data
		tv.tv_sec = 1;	// for debug
		tv.tv_usec = 200000;
		FD_ZERO(&rfds);
		FD_SET(client->m_nSockfd, &rfds);

		selVal = select(client->m_nSockfd+1, &rfds, NULL, NULL, &tv);

		if (selVal > 0) {
			memset(buf, '\0', BUFLEN);

			//try to receive some data, this is a blocking call
			if ((recv_len = recvfrom(client->m_nSockfd, buf, BUFLEN - 1, 0, (struct sockaddr *) &si_other, &slen)) == -1)
			{
				perror("VDClient::thrdMain() - recvfrom() failed with error :");
                client->m_Logger->error("VDClient::thrdMain() - recvfrom() failed with error : %d", errno);
				break;
			}

			pos = buf;
			// 인입된 UDP패킷이 RT-STT 음성 패킷이 아닌 경우 처리하지 않음
#ifdef USE_DIFF_CSCODE
			if (memcmp(pos, client->m_sCounselCode.c_str(), client->m_sCounselCode.size())) {
                client->m_Logger->warn("VDClient::thrdMain() - Invalid Voice Data Packet - VDClient(%d) recv_len(%d), pVrc(0x%p), nWorkStat(%d)", client->m_nPort, recv_len, client->m_pVrc, client->m_nWorkStat);
				continue;
			}
#else
			if (memcmp(pos, "RT-STT", 6)) {
                client->m_Logger->warn("VDClient::thrdMain() - Invalid Voice Data Packet - VDClient(%d) recv_len(%d), pVrc(0x%p), nWorkStat(%d)", client->m_nPort, recv_len, client->m_pVrc, client->m_nWorkStat);
				continue;
			}
#endif
			pos += 6;
			memcpy(&nVDSize, pos, sizeof(uint16_t));
			#ifdef FOR_ITFACT
			recv_len = ::ntohs(nVDSize)/2;
			#else
			recv_len = ::ntohs(nVDSize);
			if ( client->m_nWorkStat == 3 ) {
				client->m_Logger->info("VDClient::thrdMain() - VDClient(%d) First Data size(%d)", client->m_nPort, recv_len);
			}
			if (recv_len>640) recv_len = 640;
			#endif
			pos += sizeof(uint16_t);

			// m_nWorkStat 상태가 대기가 아닐 경우에만 recvfrom한 buf 내용으로 작업을 진행한다.
			// m_nWorkStat 상태가 대기(0)인 경우 recvfrom() 수행하여 수집된 데이터는 버림
			if (recv_len && client->m_pVrc && client->m_nWorkStat) {
				client->m_tTimeout = time(NULL);
				if (!item) {
					item = new QueItem;

                    // 시작 패킷 표시
                    if (client->m_nWorkStat == 3) {
                        item->flag = 2;
                        client->m_nWorkStat = 1;
                    }
                    else {
                        item->flag = 1;
                    }
					item->spkNo = client->m_nSpkNo;
					item->lenVoiceData = 0;
					memset(item->voiceData, 0x00, VOICE_BUFF_LEN);
				}

				memcpy(item->voiceData + item->lenVoiceData, pos, recv_len);
				item->lenVoiceData += recv_len;
                
				if (item->lenVoiceData >= client->m_nPlaytime) {
					client->m_pVrc->insertQueItem(item);
					item = NULL;
				}

			}

			if (client->m_pVrc && (client->m_nWorkStat == 2)) {	// 호 종료 요청이 들어왔을 때
			END_CALL:
                if (!HAManager::getInstance() || HAManager::getInstance()->getHAStat()) {
                    if (client->m_pVrc) {

                        client->m_Logger->debug("VDClient::thrdMain() - VDClient(%d, %s) work ending...", client->m_nPort, client->m_pVrc->getCounselCode().c_str());
                        if (!item) {
                            item = new QueItem;
                            // item->voiceData = NULL;
                            item->lenVoiceData = 0;
                            item->spkNo = client->m_nSpkNo;
							memset(item->voiceData, 0x00, VOICE_BUFF_LEN);
                        }

                        item->flag = 0;
                        if (recv_len) {
                            memcpy(item->voiceData + item->lenVoiceData, pos, recv_len);
                            item->lenVoiceData += recv_len;
                        }

                        client->m_pVrc->insertQueItem(item);
                        item = NULL;
                    }

                }
				// 작업 종료 요청 후 마지막 데이터 처리 후 상태를 대기 상태로 전환
				client->m_sCallId = "";
				client->m_sCounselCode = "";
				client->m_nSpkNo = 0;
				client->m_nWorkStat = 0;
				client->m_pVrc = NULL;

			}
		}
		else if ((selVal == 0) && ((client->m_nWorkStat == 3) || (client->m_nWorkStat == 1))) {	// 이 로직은 수정해야할 필요가 있다. 현재는 30초동안 데이터가 안들어 올 경우 호를 종료
            if (HAManager::getInstance() && !HAManager::getInstance()->getHAStat()) {
                continue;
            }
            
			// timeout : 현재 30초로 고정, timeout이 0이 아닐 경우에만
			if (nTimeout && ((time(NULL) - client->m_tTimeout) > nTimeout)) {
				WorkTracer::instance()->insertWork(client->m_sCallId, 'R', WorkQueItem::PROCTYPE::R_END_VOICE, client->m_nSpkNo);

                client->m_Logger->debug("VDClient::thrdMain(%d, %s) - Working... timeout(%llu)", client->m_nPort, client->m_pVrc->getCounselCode().c_str(), (time(NULL) - client->m_tTimeout));
				recv_len = 0;
				goto END_CALL;
			}

			if (bUseSilBuf)
			{
				// 호 진행 중 데이터가 들어오지 않을 경우 묵음 데이터 생성 및 전달, 2019-01-18
				if (!item) {
					item = new QueItem;
					// item->voiceData = new uint8_t[VOICE_BUFF_LEN];
					// 시작 패킷 표시
					if (client->m_nWorkStat == 3) {
						item->flag = 2;
						client->m_nWorkStat = 1;
					}
					else {
						item->flag = 1;
					}
					item->spkNo = client->m_nSpkNo;
					item->lenVoiceData = 0;
					memset(item->voiceData, 0x00, VOICE_BUFF_LEN);
				}

				memcpy( item->voiceData + item->lenVoiceData, silBuf, (client->m_nPlaytime - item->lenVoiceData) ) ;
				client->m_Logger->debug("VDClient::thrdMain(%d, %s) - Send Silence Data Len(%d)", client->m_nPort, client->m_pVrc->getCounselCode().c_str(), item->lenVoiceData);
				item->lenVoiceData = client->m_nPlaytime;
				client->m_pVrc->insertQueItem(item);
				item = NULL;

			}

		}
		else if ((selVal == 0) && (client->m_nWorkStat == 2)) {
			recv_len = 0;
			goto END_CALL;
		}

	}
}

VDClient::~VDClient()
{
	if (m_nSockfd) closesocket(m_nSockfd);

}

void VDClient::startWork(std::string& callid, std::string& counselcode, uint8_t spkno)
{
	m_sCallId = callid;
	m_sCounselCode = counselcode;
	m_nSpkNo = spkno;
	m_tTimeout = time(NULL);
	m_nWorkStat = uint8_t(3);

    m_pVrc = m_vrcm->getVRClient(callid);

	WorkTracer::instance()->insertWork(m_sCallId, 'R', WorkQueItem::PROCTYPE::R_BEGIN_VOICE, m_nSpkNo);
}

#endif // ENABLE_REALTIME
