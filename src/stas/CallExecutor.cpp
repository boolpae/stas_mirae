#ifdef  ENABLE_REALTIME

#include "CallExecutor.h"
#include "CallSignal.h"
#include "VRCManager.h"
#include "VDCManager.h"
#include "WorkTracer.h"

#include "DBHandler.h"

#include "HAManager.h"

#ifdef USE_REALTIME_MT
#include "VRClientMT.h"
#else
#include "VRClient.h"
#endif

#include "stas.h"

#include <thread>
#include <chrono>
#include <vector>

#ifndef WIN32
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

using namespace Protocol;

bool CallExecutor::ms_bThrdRun;

QueueItem::QueueItem(uint16_t num, SOCKET sockfd, struct sockaddr_in si, time_t tm, uint16_t psize, uint8_t* packet)
	: m_nNum(num), m_sockfd(sockfd), m_si(si), m_time(tm), m_packetSize(psize)
{
	m_packet = packet;
	// printf("\t\t[%d] QueueItem Created!\n", m_nNum);
}

QueueItem::~QueueItem()
{
	delete[] m_packet;
	// printf("\t\t[%d] QueueItem Destroyed!\n", m_nNum);
}

CallExecutor::CallExecutor(uint16_t num, VDCManager *vdcm, VRCManager *vrcm, DBHandler* st2db, HAManager *ham)
	: m_nNum(num), m_vdcm(vdcm), m_vrcm(vrcm), m_st2db(st2db), m_ham(ham)
{
	m_Logger = config->getLogger();
    m_Logger->debug("[%d] CallExecutor Created!", m_nNum);
}


CallExecutor::~CallExecutor()
{
	while (!m_Que.empty()) {
		delete m_Que.front();
		m_Que.pop();
	}

    m_Logger->debug("[%d] CallExecutor Destroyed!", m_nNum);
}

void CallExecutor::thrdMain(CallExecutor* exe)
{
	uint16_t num = exe->getExecNum();
	QueueItem* item = NULL;
	CallSignal *cs = new CallSignal();
	std::vector< uint16_t > vPorts;
	std::string sCounselorCode;
	std::string sCallId;
	std::string sDownloadPath = config->getConfig("stas.wavpath", "/home/stt/Smart-VR/input");
	uint16_t resReq;

	ms_bThrdRun = true;
	while (ms_bThrdRun) {
		while ((item = exe->popPacket())) {
			exe->m_Logger->debug("CallExecutor::thrdMain() - [%d] Received CallSignal from %s:%d", num, inet_ntoa(item->m_si.sin_addr), ntohs(item->m_si.sin_port));

			// 패킷 파싱 후 호 시작/종료 에 대한 처리 및 응답 패킷 생성 후 응답 로직
			cs->init();
			if ((resReq = cs->parsePacket(item->m_packet)) == 200) {
				
                exe->m_Logger->info("CallExecutor::thrdMain() - [%d] Received CallSignal from %s:%d(%s : %s)", num, inet_ntoa(item->m_si.sin_addr), ntohs(item->m_si.sin_port), cs->getCounselorCode(), cs->getCallId());
				
				sCounselorCode = std::string(cs->getCounselorCode());
                sCallId = std::string(cs->getCallId());

				// 호(Call) 시작 요청
				if (cs->getPacketFlag() == 'B') {
					time_t startT;
					WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_BEGIN_PROC);
					// VDC, VRC 요청
					// 1. VRC 요청 : 성공 시 VDC 요청, 실패 패킷 생성하여 sendto
					WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_REQ_WORKER);
#ifdef EN_RINGBACK_LEN
                    if ((resReq = exe->m_vrcm->requestVRC(sCallId, sCounselorCode, startT, 'R', cs->getUdpCnt(), cs->getRingbackLen())))
#else
                    if ((resReq = exe->m_vrcm->requestVRC(sCallId, sCounselorCode, startT, 'R', cs->getUdpCnt())))
#endif
					{	// VRClient 요청 실패한 경우

						WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_RES_WORKER);

						if (resReq == 1) {
							// ERRCODE: 500 - 내부 서버 오류
							cs->makePacket(item->m_packet, item->m_packetSize, 500);
						}
						else if (resReq == 2) {
							// ERRCODE: 503 - 서비스를 사용할 수 없음
							cs->makePacket(item->m_packet, item->m_packetSize, 503);
						}
						else if (resReq == 3) {
							// ERRCODE: 504 - Gearman 호스트 연결/통신 실패
							cs->makePacket(item->m_packet, item->m_packetSize, 504);
						}
                        exe->m_Logger->error("CallExecutor::thrdMain() - [%d] Failed requestVRC() from %s:%d(%s) - (%d)", num, inet_ntoa(item->m_si.sin_addr), ntohs(item->m_si.sin_port), cs->getCallId(), resReq);
					}
					// 2. VDC 요청 : 성공 시 성공 패킷 생성하여 sendto, 실패 시 removeVRC 수행 후 실패 패킷 생성하여 sendto
					else {
						WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_RES_WORKER, 1);
						WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_REQ_CHANNEL);
						if ((resReq = exe->m_vdcm->requestVDC(sCallId, cs->getUdpCnt(), vPorts))) {
							// ERRCODE: 507 - 용량부족
							WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_RES_CHANNEL, 0);
                            exe->m_vrcm->removeVRC(sCallId);
							cs->makePacket(item->m_packet, item->m_packetSize, 507);
                            exe->m_Logger->error("CallExecutor::thrdMain() - [%d] Failed requestVDC() from %s:%d(%s) - (%d)", num, inet_ntoa(item->m_si.sin_addr), ntohs(item->m_si.sin_port), cs->getCallId(), resReq);
						}
						else {
							// SUCCESS
                            // to DB
                            if (exe->m_st2db) {
								// 조회, 존재할 경우 UPDATE, 없을 경우 INSERT
								if (exe->m_st2db->searchCallInfo(sCounselorCode)>0)
                                	exe->m_st2db->updateCallInfo(sCounselorCode, sCallId, false);
								else
									exe->m_st2db->insertCallInfo(sCounselorCode, sCallId);

								std::string ext = ".wav";
								std::string filename = sCallId + ext;
								
								exe->m_st2db->insertTaskInfoRT(sDownloadPath, filename, sCallId, sCounselorCode, startT);
									
                            }
							WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_RES_CHANNEL, 1);
							cs->makePacket(item->m_packet, item->m_packetSize, vPorts);
                            
                            // HA
                            if (exe->m_ham)
#ifdef EN_RINGBACK_LEN
                                exe->m_ham->insertSyncItem(true, sCallId, sCounselorCode, exe->m_vrcm->getVRClient(sCallId)->getFname(), vPorts[0], vPorts[1], cs->getRingbackLen());
#else
                                exe->m_ham->insertSyncItem(true, sCallId, sCounselorCode, exe->m_vrcm->getVRClient(sCallId)->getFname(), vPorts[0], vPorts[1]);
#endif
						}
					}
				}
				// 호(Call) 종료 요청
				else if (cs->getPacketFlag() == 'E') {

					WorkTracer::instance()->insertWork(sCallId, 'R', WorkQueItem::PROCTYPE::R_END_PROC);
                    exe->m_vdcm->removeVDC(sCallId);
					cs->makePacket(item->m_packet, item->m_packetSize, 200);

				}
			}
			else {	// 프로토콜 데이터 파싱 실패
                exe->m_Logger->error("CallExecutor::thrdMain() - [%d] Error parse packet from %s:%d (%d)", num, inet_ntoa(item->m_si.sin_addr), ntohs(item->m_si.sin_port), resReq);
				switch (resReq) {
				case 404:	// 패킷 형태 오류 : 잘 못 된 패킷에 대한 응답
					cs->makePacket(item->m_packet, item->m_packetSize);
					break;
				default:
					cs->makePacket(item->m_packet, item->m_packetSize, resReq);
				}
			}

			sendto(item->m_sockfd, (const char*)cs->getPacket(), cs->getPacketSize(), 0, (struct sockaddr *)&item->m_si, sizeof(struct sockaddr_in));

            exe->m_Logger->debug("CallExecutor::thrdMain() - [%d] Send response packet from %s:%d", num, inet_ntoa(item->m_si.sin_addr), ntohs(item->m_si.sin_port));
            
			vPorts.clear();
			delete item;
			item = NULL;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	delete cs;
}

// CallReciever에 의해서 사용되는 함수로서 STT-Parrot으로부터 받은 패킷을 CallExecutor에게 전달할 때 사용됨
void CallExecutor::pushPacket(SOCKET sockfd, struct sockaddr_in si, uint16_t psize, uint8_t* packet)
{
	std::lock_guard<std::mutex> g(m_mxQue);

	QueueItem* item = new QueueItem(this->m_nNum, sockfd, si, time(NULL), psize, packet);
	m_Que.push(item);
}

// CallExecutor에서 사용되며 CallReciever로부터 전달된 STT-Parrot의 요청을 가져올 때 사용
QueueItem* CallExecutor::popPacket()
{
	QueueItem* item;

	std::lock_guard<std::mutex> g(m_mxQue);

	if (m_Que.empty()) return NULL;

	item = m_Que.front();
	m_Que.pop();

	return item;
}

#endif // ENABLE_REALTIME
