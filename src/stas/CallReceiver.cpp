#ifdef  ENABLE_REALTIME

#ifdef WIN32

#include <WS2tcpip.h>	// for socklen_t type

#pragma comment(lib,"ws2_32.lib") //Winsock Library

#else

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#endif

#include "CallReceiver.h"
#include "CallExecutor.h"
#include "VDCManager.h"
#include "VRCManager.h"

#ifndef USE_ODBC
#include "DBHandler.h"
#else
#include "DBHandler_ODBC.h"
#endif

#include "HAManager.h"
#include "stas.h"

#include <thread>

CallReceiver* CallReceiver::m_instance = NULL;

CallReceiver::CallReceiver(VDCManager *vdcm, VRCManager *vrcm, /*log4cpp::Category *logger,*/ DBHandler* st2db, HAManager *ham)
	: m_nSockfd(0), m_nNumofExecutor(3), m_vdcm(vdcm), m_vrcm(vrcm), /*m_Logger(logger),*/ m_st2db(st2db), m_ham(ham)
{
	//printf("\t[DEBUG] CallReceiver Constructed.\n");
	m_Logger = config->getLogger();
    m_Logger->debug("CallReceiver Constructed.");
}


CallReceiver::~CallReceiver()
{
	if (m_nSockfd) closesocket(m_nSockfd);

	//printf("\t[DEBUG] CallReceiver Destructed.\n");
    m_Logger->debug("CallReceiver Destructed.");
}

CallReceiver* CallReceiver::instance(VDCManager *vdcm, VRCManager *vrcm, /*log4cpp::Category *logger,*/ DBHandler* st2db, HAManager *ham)
{
	if (m_instance) return m_instance;

	m_instance = new CallReceiver(vdcm, vrcm, /*logger,*/ st2db, ham);
	return m_instance;
}

void CallReceiver::release()
{
	if (m_instance)
	{
		delete m_instance;
		m_instance = NULL;
	}
}

#define BUFLEN 512  //Max length of buffer
void CallReceiver::thrdMain(CallReceiver* rcv)
{
	char buf[BUFLEN];
	struct sockaddr_in si_other;
	int recv_len;
	socklen_t slen = sizeof(si_other);
	uint16_t noe, coe;
	vector< CallExecutor* > vExes;
	vector< std::thread* > vThrds;
	uint8_t* packet;

	noe = rcv->m_nNumofExecutor;
	coe = 0;

	if (!rcv->m_nSockfd)
	{
		//printf("CallReceiver::thrdMain() - not exist socket\n");
        rcv->m_Logger->error("CallReceiver::thrdMain() - not exist socket");
		return;
	}

	for (int i = 0; i < noe; i++) {
		vExes.push_back(new CallExecutor(i+1, rcv->m_vdcm, rcv->m_vrcm, /*rcv->m_Logger,*/ rcv->m_st2db, rcv->m_ham));
	}

	for (int i = 0; i < noe; i++) {
		std::thread *thrd = new std::thread(CallExecutor::thrdMain, vExes[i]);
		//thrd->detach();
		vThrds.push_back(thrd);
	}

	while (1)
	{
		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);

		//try to receive some data, this is a blocking call
		if ((recv_len = recvfrom(rcv->m_nSockfd, buf, BUFLEN-1, 0, (struct sockaddr *) &si_other, &slen)) == -1)
		{
			perror("CallReceiver::thrdMain() - recvfrom() failed with error :");
            rcv->m_Logger->error("CallReceiver::thrdMain() - recvfrom() failed with error : %d", errno);
			break;
		}

		packet = new uint8_t[recv_len+1];
		memset(packet, 0x00, recv_len + 1);
		memcpy(packet, buf, recv_len);
		vExes[coe++]->pushPacket(rcv->m_nSockfd, si_other, recv_len, packet);

		if (noe == coe) coe = 0;

		//print details of the client/peer and the data received
#if 0
		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printf("Received packet size: %d\n", recv_len);
#endif
        rcv->m_Logger->debug("CallReceiver::thrdMain() - Received packet from %s:%d, size: %d", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), recv_len);
	}

	CallExecutor::thrdFinish();
	while (!vThrds.empty()) {
		vThrds.back()->join();
		delete vThrds.back();
		vThrds.pop_back();
	}
	while (!vExes.empty()) {
		delete vExes.back();
		vExes.pop_back();
	}
}

bool CallReceiver::init(uint16_t port)
{
	struct sockaddr_in addr;
	if ((m_nSockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("CallReceiver::init() :");
        m_Logger->error("CallReceiver::init() - failed get socket : %d", errno);
		return false;
	}

	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (::bind(m_nSockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("CallReceiver::init() - bind : ");
        m_Logger->error("CallReceiver::init() - bind : %d", errno);
		closesocket(m_nSockfd);
		return false;
	}

	std::thread callRcvThrd(CallReceiver::thrdMain, this);
	callRcvThrd.detach();

	return true;
}


#endif // ENABLE_REALTIME