#ifdef  ENABLE_REALTIME

#include "VDCManager.h"
#include "VRCManager.h"
#include "VDClient.h"
#include "stas.h"

VDCManager* VDCManager::ms_instance = NULL;

VDCManager::VDCManager(uint32_t pt, VRCManager *vrcm)
: m_nPlaytime(pt), m_vrcm(vrcm)
{
	m_Logger = config->getLogger();
    m_Logger->debug("VDCManager Constructed.");
}


VDCManager::~VDCManager()
{
	std::vector< VDClient* >::iterator iter;
	for (iter = m_vClients.begin(); iter != m_vClients.end(); iter++) {
		((VDClient*)(*iter))->finish();
	}
	m_vClients.clear();

    m_Logger->debug("VDCManager Destructed.");
}

VDCManager* VDCManager::instance(uint16_t tcount, uint16_t bport, uint16_t eport, uint32_t pt, VRCManager *vrcm)
{
	VDClient* client;
	int i = 0;
	std::vector< VDClient* >::iterator iter;

	if (ms_instance) return ms_instance;

	ms_instance = new VDCManager(pt, vrcm);

	// TOTAL_CHANNEL_COUNT : 생성할 채널의 총 갯수
	// BEGIN_PORT, END_PORT : 음성 데이터를 받기 위한 UDP포트의 범위
	// 총 채널의 갯수만큼 VDClient를 만들지 못 한 경우 VDCManager instance 생성 실패
	while (i++ < tcount) {
		client = new VDClient(vrcm);
        client->setPlaytime(pt);

		while (client->init(bport)) {
			log4cpp::Category *logger = config->getLogger();
            logger->error("VDCManager::instance() - init(%d) error", bport);
			bport++;
			if (bport > eport)
				break;
		}

		if (bport < eport) {
			ms_instance->m_vClients.push_back(client);
		}
		else {
			VDCManager::release();
			return NULL;
		}

		bport++;
	}
	
	return ms_instance;
}

void VDCManager::release()
{
	if (ms_instance) {
		delete ms_instance;
		ms_instance = NULL;
	}
}

// return : 성공(0), 실패(0이 아닌 값)
int16_t VDCManager::requestVDC(std::string & callid, std::string & counselcode, uint8_t noc, std::vector< uint16_t > &vPorts)
{
	int16_t res = 0;
	uint8_t coc = 0;
	std::vector< VDClient* >::iterator iter;
	std::vector< VDClient* > clients;

	std::lock_guard<std::mutex> g(m_mxVec);

	for (iter = m_vClients.begin(); iter != m_vClients.end(); iter++) {
		if (!((VDClient*)(*iter))->getWorkStat()) {	// 현재 대기중인 VDClient를 찾음
			if (coc < noc) {
				clients.push_back((*iter));
				coc++;
			}
			
			if ( coc == noc ) {
				break;
			}
		}
	}

	if (clients.size() == noc) {
		coc = 1;
		for (iter = clients.begin(); iter != clients.end(); iter++) {
			vPorts.push_back( (*iter)->getPort() );	// 할당 성공한 채널의 포트를 CallExecutor로 전달
			(*iter)->startWork(callid, counselcode, coc++);
		}
	}
	else {
		res = 1;	// VDClient 할당 요청하였으나 noc 갯수 만큼 요청 받지 못 함.
	}
	clients.clear();

	return res;
}

void VDCManager::removeVDC(std::string callid)
{
	std::vector< VDClient* >::iterator iter;

	std::lock_guard<std::mutex> g(m_mxVec);

	for (iter = m_vClients.begin(); iter != m_vClients.end(); iter++) {
		if (!((VDClient*)(*iter))->getCallId().compare(callid)) {
			((VDClient*)(*iter))->stopWork();
		}
	}

}

void VDCManager::outputVDCStat()
{
    int vdccount=0;
	std::vector< VDClient* >::iterator iter;

	for (iter = m_vClients.begin(); iter != m_vClients.end(); iter++) {
        if (((VDClient*)(*iter))->getWorkStat()) {
            vdccount++;
        }
	}
    
    if ( vdccount )
        m_Logger->info("VDCManager::outputVDCStat() - Current working VDClient count(%d)", vdccount);
}

int VDCManager::setActiveVDC(std::string callid, std::string counselcode, uint8_t spkno, uint16_t port)
{
	int16_t res = 0;
	std::vector< VDClient* >::iterator iter;

	std::lock_guard<std::mutex> g(m_mxVec);

	for (iter = m_vClients.begin(); iter != m_vClients.end(); iter++) {
		if (((VDClient*)(*iter))->getPort() == port) {	// 현재 대기중인 VDClient를 찾음
            (*iter)->startWork(callid, counselcode, spkno);
            break;
		}
	}
    
    if (iter == m_vClients.end()) {
        res = 1;  // 요청한 port를 가진 client가 없을 경우
        m_Logger->error("VDCManager::setActiveVDC() - can't find port(%d) error", port);
    }

    return res;
}

#endif // ENABLE_REALTIME