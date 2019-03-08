
#include "VFCManager.h"

#include <unistd.h>
#include <string.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "VFClient.h"
#include "stas.h"

#include "DBHandler.h"

using namespace std;

VFCManager* VFCManager::m_instance = nullptr;
uint64_t VFCManager::m_nVFCs=0;

VFCManager::VFCManager(int geartimeout)
	: m_sGearHost("127.0.0.1"), m_nGearPort(4730), m_GearTimeout(geartimeout), m_nSockGearman(0)
{
    m_Logger = config->getLogger();
    m_bMakeMLF = !config->getConfig("stt_result.make_mlf", "false").compare("true");
    m_sResultPath = config->getConfig("stt_result.path", "./stt_result");
    m_Logger->debug("VFCManager Constructed.");
}


VFCManager::~VFCManager()
{
    map< string, VFClient* >::iterator iter;
    while(m_mWorkerTable.size()) {
        iter = m_mWorkerTable.begin();
        ((VFClient*)iter->second)->stopWork();
        m_mWorkerTable.erase(iter);
    }
    
    m_Logger->debug("VFCManager Destructed.");
}

bool VFCManager::connectGearman()
{
	struct sockaddr_in addr;

	if (m_nSockGearman) closesocket(m_nSockGearman);

	if ((m_nSockGearman = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {//소켓 생성
		perror("VFCManager::connectGearman() - socket :");
        m_Logger->error("VFCManager::connectGearman() - socket : %d", errno);
		m_nSockGearman = 0;
		return false;
	}

	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(m_sGearHost.c_str());
	addr.sin_port = htons(m_nGearPort);

	if (::connect(m_nSockGearman, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("VFCManager::connectGearman() - connect :");
        m_Logger->error("VFCManager::connectGearman() - connect : %d", errno);
		closesocket(m_nSockGearman);
		m_nSockGearman = 0;
		return false;
	}

	return true;
}

void VFCManager::disconnectGearman()
{
	if (m_nSockGearman > 0) {
		closesocket(m_nSockGearman);
        m_nSockGearman = 0;
	}
}

#define RECV_BUFF_LEN 512
size_t VFCManager::getWorkerCount()
{
	char recvBuf[RECV_BUFF_LEN];
	int recvLen = 0;
	std::string sReq = "status\r\n";
	std::string sRes = "";
	int rec = 0;

	struct timeval tv;
	fd_set rfds;
	int selVal;

	RECONNECT:
	if (!m_nSockGearman && !connectGearman()) {
        m_Logger->error("VFCManager::getWorkerCount() - error connect to GearHost.");
		return 0;
	}

	if ( ::send(m_nSockGearman, sReq.c_str(), sReq.length(), 0) <= 0) {
		perror("VFCManager::getWorkerCount() - send :");
        m_Logger->error("VFCManager::getWorkerCount() - send : %d", errno);
		if (++rec > 3) {
            disconnectGearman();
            m_Logger->warn("VFCManager::getWorkerCount() - error Reconnect count 3 exceeded.");
			return 0;
		}
        disconnectGearman();
		goto RECONNECT;
	}

	while (1) {
		tv.tv_sec = 0;	// for debug
		tv.tv_usec = 500000;
		FD_ZERO(&rfds);
		FD_SET(m_nSockGearman, &rfds);

		selVal = select(m_nSockGearman+1, &rfds, NULL, NULL, &tv);

        if (selVal > 0) {
            recvLen = ::recv(m_nSockGearman, recvBuf, RECV_BUFF_LEN-1, 0);
            if (recvLen <= 0) {
                perror("VFCManager::getWorkerCount() - send :");
                disconnectGearman();
                m_Logger->error("VFCManager::getWorkerCount() - send : %d", errno);
                return 0;
            }
            recvBuf[recvLen] = 0;
            sRes.append(recvBuf);

            if ( !strncmp( (sRes.c_str() + (sRes.length() - 2)), ".\n", 2) ) {	// 마지막 문장이 ".\r\n" 인 경우 루프 탈출
                break;
            }
        }
        else if (selVal == 0) {
            m_Logger->error("VFCManager::getWorkerCount() - recv timeout : reconnect(%d)", rec);
            if (++rec > 3) {
                disconnectGearman();
                m_Logger->warn("VFCManager::getWorkerCount() - error Reconnect count 3 exceeded.");
                return 0;
            }
            disconnectGearman();
            goto RECONNECT;
        }
	}

    disconnectGearman();
	
	return getWorkerCountFromString(sRes);
}

size_t VFCManager::getWorkerCountFromString(std::string & gearResult)
{
	std::string token;
	char fname[64];
	uint16_t c=0;
	uint16_t r=0;
	uint16_t w=0;
	size_t pos, npos;

	pos = npos = 0;

	while ((npos = gearResult.find("\n", pos)) != string::npos) {

		token = gearResult.substr(pos, npos - pos);

		if (!strncmp(token.c_str() + (token.length() - 1), ".", 1)) {
            w=0;
			break;
		}

		sscanf(token.c_str(), "%s\t%hu\t%hu\t%hu", fname, &c, &r, &w);

		if (!strncmp(fname, "vr_stt", 6)) {
            break;
		}
        w=0;

		pos = npos + 1;
	}
    
    return size_t(w);

}

VFCManager* VFCManager::instance(const std::string gearHostIp, const uint16_t gearHostPort, int geartimeout)
{
	if (m_instance) return m_instance;

	m_instance = new VFCManager(geartimeout);

	// for DEV
	m_instance->setGearHost(gearHostIp);
	m_instance->setGearPort(gearHostPort);
    
    std::thread thrd = std::thread(VFCManager::thrdFuncVFCManager, m_instance);
    thrd.detach();

	return m_instance;
}

void VFCManager::release()
{
	if (m_instance) {
		delete m_instance;
		m_instance = NULL;
	}
}

void VFCManager::outputVFCStat()
{
    if ( m_mWorkerTable.size() )
        m_Logger->debug("VFCManager::outputVRCStat() - Current working VFClient count(%d)", m_mWorkerTable.size());

}

int VFCManager::pushItem(JobInfoItem* item)
{
	std::lock_guard<std::mutex> g(m_mxQue);
    
    m_qVFQue.push(item);
    
    m_Logger->debug("VFCManager::pushItem() - Item Count(%d)", m_qVFQue.size());
    
    return m_qVFQue.size();
}

JobInfoItem* VFCManager::popItem()
{
	std::lock_guard<std::mutex> g(m_mxQue);
    JobInfoItem* item = nullptr;
    
    if (!m_qVFQue.size()) return nullptr;
    
    item = m_qVFQue.front();
    m_qVFQue.pop();
    
    m_Logger->debug("VFCManager::popItem() - Item Content(%s), Count(%d)", item->getCallId().c_str(), m_qVFQue.size());

    return item;
}

void VFCManager::thrdFuncVFCManager(VFCManager* mgr)
{
    mgr->connectGearman();
    
    while(1) {
        // gearman 호스트에 접속 후 file을 처리할 worker의 갯수 파악
        // 파악된 worker의 갯수 만큼 VFClient 생성 후 테이블로 관리
        // 만약 파악 된 worker의 갯수와 테이블에 등록된 VFClient의 갯수가 다를 경우 이를 관리(생성 및 소멸)
        mgr->syncWorkerVFClient();
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    mgr->disconnectGearman();
}

void VFCManager::syncWorkerVFClient()
{
    char szKey[20];
    size_t workerCnt = getWorkerCount();
    
    m_Logger->debug("VFCManager::syncWorkerVFClient() - worker_count(%d), table_size(%d)", workerCnt, m_mWorkerTable.size());
    
    // worker의 갯수와 map에 등록된 VFClient의 갯수를 비교
    if (workerCnt > m_mWorkerTable.size()) {
        struct timeval tv;
        VFClient *clt;
        // 두 obj의 갯수가 다를 경우 이 갯수를 동일하게 관리
        while(workerCnt - m_mWorkerTable.size()) {
            gettimeofday(&tv, NULL);
            sprintf(szKey, "%ld.%ld", tv.tv_sec, tv.tv_usec);
            clt = new VFClient(this, this->m_sGearHost, this->m_nGearPort, this->m_GearTimeout, m_nVFCs++, m_bMakeMLF, m_sResultPath);
            m_mWorkerTable[szKey] = clt;
            clt->startWork();
        }
        
    }
    else if (workerCnt < m_mWorkerTable.size()) {
        map< string, VFClient* >::iterator iter;
        while(m_mWorkerTable.size() - workerCnt) {
            iter = m_mWorkerTable.begin();
            ((VFClient*)iter->second)->stopWork();
            m_nVFCs--;
            m_mWorkerTable.erase(iter);
        }
    }
}

int VFCManager::getAvailableCount()
{
    int aCnt=0;

    aCnt = m_mWorkerTable.size() - m_qVFQue.size();
    
    return aCnt;
}
