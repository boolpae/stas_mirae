// TestCallClient.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>

#include "CallSignal.h"

#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

#define BUFLEN 512
#define PCMBUFLEN 16
#define SOCKET int

using namespace std;

static volatile int gTotalCall = 0;
std::mutex gMutex;

static void increaseCallCount() {
    std::lock_guard<std::mutex> g(gMutex);
    gTotalCall++;
}

static void decreaseCallCount() {
    std::lock_guard<std::mutex> g(gMutex);
    gTotalCall--;
}

static void thrdMain(std::string callid, std::string ipaddr, unsigned int port, std::string pcmfilename, int blockSize, int sleeptime, int sendpaccnt)
{
	std::ifstream pcmfile(pcmfilename, std::ios::in | std::ios::binary);
	char pcmbuf[65000];
	SOCKET s;
	struct sockaddr_in si_other;
	socklen_t slen;
	uint16_t pSize;
    int nSendPacketCount = sendpaccnt;
    int nPacketCount = 0;

	//std::cout << "[" << callid << "] PCM Data Sending...[" << std::to_string(port) << "] : " << pcmfilename << std::endl;

	//setup address structure
	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(port);
	si_other.sin_addr.s_addr = inet_addr(ipaddr.c_str());

	slen = sizeof(si_other);

	//create socket
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		printf("socket() failed with error code : %d", errno);
		exit(EXIT_FAILURE);
	}

	if (pcmfile.is_open()) {
        if (pcmfilename.find(".wav") != std::string::npos) pcmfile.read(pcmbuf, 46);
		memcpy(pcmbuf, "RT-STT", 6);
		while (!pcmfile.eof()) {
			pcmfile.read(pcmbuf+8, blockSize);
			pSize = htons(pcmfile.gcount());
			memcpy(pcmbuf + 6, &pSize, sizeof(uint16_t));

			//send the message
			if (sendto(s, (const char*)pcmbuf, pcmfile.gcount() + 8, 0, (struct sockaddr *) &si_other, slen) == -1)
			{
				printf("sendto() failed with error code : %d", errno);
				exit(EXIT_FAILURE);
			}
            nPacketCount++;

            if (sendpaccnt) {
                if (--nSendPacketCount <= 0) break;
            }
			std::this_thread::sleep_for(std::chrono::milliseconds(sleeptime));
		}
		pcmfile.close();
	}

    std::cout << "[" << callid << "] PCM Data Count [" << std::to_string(nPacketCount) << "]" << std::endl;

	close(s);
}

static void thrdCall(int idx, std::string sSvrIp, int nPort, int nSleeptime, char cFlag, std::string sCallId, int nUdpCount, int nSampleRate, int nChannelCount, int nEncoding, std::string sPcmFile1, std::string sPcmFile2, int nSendBlockSize)
{
    timeval lt;
    char timebuff[80];
    int milli;
    int nSendPacketCount = 0;
    std::string sFingerPrint;
    time_t tCurrTime;
    char buf[BUFLEN];
    socklen_t slen;
    std::vector< std::string > vPcmFiles;
    std::vector< std::thread > vThrds;

    char rescode[4];
    unsigned short port;


    SOCKET s;
    struct sockaddr_in si_other;

    Protocol::CallSignal* cs;

    std::string sCounselorCode = std::to_string(idx);


    // std::cout << "THREAD ID: (" << std::this_thread::get_id() << "), CallID: (" << sCallId << ")" << std::endl;
    std::ostringstream ss;
    ss << std::this_thread::get_id();
    std::string sThreadId = ss.str();


    vPcmFiles.push_back(sPcmFile1);
    vPcmFiles.push_back(sPcmFile2);
#if 0
    printf("\n\n *-- [INFO : %lu] --*\n"
        " Server IP       : [%s]\n"
        " Server Port     : [%d]\n"
        " Sleeptime       : [%d]\n"
        " Call Type       : [%c]\n"
        " Call ID         : [%s]\n"
        " UDP Count       : [%d]\n"
        " Sample Rate Hz  : [%d]\n"
        " Encoding        : [%d]\n"
        " Channel Count   : [%d]\n"
        " PCM Filename(%d) : [%s]\n"
        " PCM Filename(%d) : [%s]\n"
        , std::this_thread::get_id()
        , sSvrIp.c_str(), nPort, nSleeptime, cFlag, sCallId.c_str(), nUdpCount, nSampleRate, nEncoding, nChannelCount, 0, vPcmFiles[0].c_str(), 1, vPcmFiles[1].c_str());
#endif
#if 1


    cs = new Protocol::CallSignal();
    cs->setPacFlag(cFlag);
    cs->setPacCounselorCode((uint8_t*)sCounselorCode.c_str(), sCounselorCode.length());
    cs->setPacCallId((uint8_t*)sCallId.c_str(), sCallId.length());
    cs->setPacUdpCnt(nUdpCount);
    cs->setPacSampleRate(nSampleRate);
    cs->setPacEnc(nEncoding);
    cs->setPacChnCnt((uint8_t)nChannelCount);
    tCurrTime = time(NULL);
    sFingerPrint = std::to_string(tCurrTime);
    cs->setFingerPrint((uint8_t*)sFingerPrint.c_str(), sFingerPrint.length());
#ifdef EN_RINGBACK_LEN
    cs->setRingbackLen(0);
#endif
    cs->makePacket(cFlag);

    //cs->printPacketInfo();


    //create socket
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        printf("socket() failed with error code : %d", errno);
        exit(EXIT_FAILURE);
    }

    //setup address structure
    memset((char *)&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(nPort);
    si_other.sin_addr.s_addr = inet_addr(sSvrIp.c_str());

    slen = sizeof(si_other);


    //send the message
    if (sendto(s, (const char*)cs->getPacket(), cs->getPacketSize(), 0, (struct sockaddr *) &si_other, slen) == -1)
    {
        printf("sendto() failed with error code : %d", errno);
        exit(EXIT_FAILURE);
    }

    //receive a reply and print it
    //clear the buffer by filling null, it might have previously received data
    memset(buf, '\0', BUFLEN);
    //try to receive some data, this is a blocking call
    if (recvfrom(s, buf, BUFLEN - 1, 0, (struct sockaddr *) &si_other, &slen) == -1)
    {
        printf("recvfrom() failed with error code : %d", errno);
        exit(EXIT_FAILURE);
    }

    memset(rescode, 0, sizeof(rescode));
    if (cFlag == 'B')
        memcpy(rescode, buf + CALL_BEG_PACKET_LEN, 3);
    else
        memcpy(rescode, buf + CALL_END_PACKET_LEN, 3);
    // GetLocalTime(&lt);
    gettimeofday(&lt, NULL);
    milli = lt.tv_usec / 1000;
    strftime(timebuff, sizeof(timebuff), "%Y%m%d-%H%M%S", localtime(&lt.tv_sec));
    printf("\t[%s] - RESCODE : [%s] File(%s, %s) - (%s.%03d)\n", sCallId.c_str(), rescode, sPcmFile1.c_str(), sPcmFile2.c_str(), timebuff, milli);
    if ((cFlag == 'B') && !strncmp(rescode, "200", 3)) {
        for (int i = 0; i < nUdpCount; i++) {
            memcpy(&port, buf + CALL_BEG_PACKET_LEN + 3 + (i * 2), 2);
            printf("\t[%s] - Port : %d\n", sCallId.c_str(), ntohs(port));

            vThrds.push_back(std::thread(thrdMain, sCallId, sSvrIp, ntohs(port), vPcmFiles[i], nSendBlockSize, nSleeptime, nSendPacketCount));
        }

        for (int i = 0; i < nUdpCount; i++) {
            vThrds[i].join();
        }

        cs->setPacFlag('E');
        cs->setPacCounselorCode((uint8_t*)sCounselorCode.c_str(), sCounselorCode.length());
        cs->setPacCallId((uint8_t*)sCallId.c_str(), sCallId.length());
        tCurrTime = time(NULL);
        sFingerPrint = std::to_string(tCurrTime);
        cs->setFingerPrint((uint8_t*)sFingerPrint.c_str(), sFingerPrint.length());
        cs->makePacket('E');

        // cs->printPacketInfo();

        //send the message
        if (sendto(s, (const char*)cs->getPacket(), cs->getPacketSize(), 0, (struct sockaddr *) &si_other, slen) == -1)
        {
            printf("sendto() failed with error code : %d", errno);
            exit(EXIT_FAILURE);
        }

        //receive a reply and print it
        //clear the buffer by filling null, it might have previously received data
        memset(buf, '\0', BUFLEN);
        //try to receive some data, this is a blocking call
#if 0
        if (recvfrom(s, buf, BUFLEN - 1, 0, (struct sockaddr *) &si_other, &slen) == SOCKET_ERROR)
        {
            printf("recvfrom() failed with error code : %d", WSAGetLastError());
            exit(EXIT_FAILURE);
        }
#endif
    }
    gettimeofday(&lt, NULL);
    milli = lt.tv_usec / 1000;
    strftime(timebuff, sizeof(timebuff), "%Y%m%d-%H%M%S", localtime(&lt.tv_sec));
    printf("\t[%s] - CALL FINISH :  File(%s, %s) - (%s.%03d)\n", sCallId.c_str(), sPcmFile1.c_str(), sPcmFile2.c_str(), timebuff, milli);

    close(s);

    decreaseCallCount();

#endif
}

int main(int argc, char* argv[])
{
    timeval lt;
    char timebuff[80];
    int milli;
    char szCallId[64];

	std::string sInput = "192.168.0.220";
	std::string sSvrIp = "192.168.0.220";
	std::string sPcmFile1;
    std::string sPcmFile2;
    int nPort = 4098;
	char cFlag = 'B';
	int nUdpCount = 2;
	int nSampleRate = 8000;
    int nChannelCount = 1;
    int nEncoding = 16;
	int nSleeptime = 20;
    int nSendPacketCount = 0;
    int nSendBlockSize = 320;
	std::string sCallId;
	std::string sFingerPrint;
	time_t tCurrTime;
	char buf[BUFLEN];
	socklen_t slen;
	std::vector< std::string > vPcmFiles;
	std::vector< std::thread > vThrds;
    std::thread currThrd;

    std::ifstream pcmfile;
    std::string line;

    char isloop[8];

    int nThrdNum = 0;
    int i = 0;

	char rescode[4];
	unsigned short port;

	SOCKET s;
	struct sockaddr_in si_other;

	Protocol::CallSignal* cs;

    //std::cout << "Input Server IP : ";
    sSvrIp = argv[1];

    //std::cout << "Input Server Port : ";
    nPort = atoi(argv[2]);

    //std::cout << "Input Sleeptime(milliseconds) : ";
    nSleeptime = 20;

    //std::cout << "Call Begin or End? (B/E) : ";
    cFlag = 'B';

    //std::cout << "Input Call-ID(less than 32bytes) : ";
    gettimeofday(&lt, NULL);
    milli = lt.tv_usec / 1000;
    strftime(timebuff, sizeof(timebuff), "%Y%m%d-%H%M%S", localtime(&lt.tv_sec));
    sprintf(szCallId, "%s.%03d", timebuff, milli);
    sCallId = szCallId;

    nUdpCount = 2;
    nSampleRate = 8000;
    nChannelCount = 1;
    nEncoding = 16;

    //sPcmFile1 = argv[3];
    //sPcmFile2 = argv[4];
    pcmfile.open(argv[3], std::ifstream::in);
    if (pcmfile.fail()) {
        std::cout << "Failed to open file(" << argv[3] << ")" << std::endl;
        return 0;
    }

    while (!pcmfile.eof()) {
        getline(pcmfile, line, '\n');
        if (line.size() > 0) {
            vPcmFiles.push_back(line);
            // std::cout << "Voice File : " << line << std::endl;
        }
    }
    std::cout << "Voice file count : " << vPcmFiles.size() << std::endl;
    pcmfile.close();

    if (vPcmFiles.size() == 0) {
        std::cout << "Error occured : wav(pcm) files not found." << std::endl;
        return 0;
    }

    sprintf(isloop, "%s", "stop");
    if (argc < 5) {
        nThrdNum = vPcmFiles.size() / 2;
    }
    else if (vPcmFiles.size() > atoi(argv[4])) {
        nThrdNum = atoi(argv[4]);
    }
    else {
        nThrdNum = vPcmFiles.size();
    }

    if (argc == 6) {
        sprintf(isloop, "%s", argv[5]);
    }

    nSendBlockSize = 320;

    if (argc < 5)
    {
        std::thread thrd;

        for (i = 0; i < nThrdNum; i++) {

            gettimeofday(&lt, NULL);
            milli = lt.tv_usec / 1000;
            strftime(timebuff, sizeof(timebuff), "%Y%m%d-%H%M%S", localtime(&lt.tv_sec));
            sprintf(szCallId, "%s.%03d", timebuff, milli);
            sCallId = szCallId;

            sPcmFile1 = vPcmFiles[i * 2];

            sPcmFile2 = vPcmFiles[i * 2 + 1];
            sprintf(szCallId, "%s.%d", sCallId.c_str(), i);
            thrd = std::thread(thrdCall, i%10, sSvrIp, nPort, nSleeptime, cFlag, std::string(szCallId), nUdpCount, nSampleRate, nChannelCount, nEncoding, sPcmFile1, sPcmFile2, nSendBlockSize);

            thrd.join();
        }
        return 0;
    }

    do {
        for (i = 0; i < nThrdNum; i++) {

            //while (1) {
            if (gTotalCall < nThrdNum) {
                gettimeofday(&lt, NULL);
                milli = lt.tv_usec / 1000;
                strftime(timebuff, sizeof(timebuff), "%Y%m%d-%H%M%S", localtime(&lt.tv_sec));
                sprintf(szCallId, "%s.%03d", timebuff, milli);
                sCallId = szCallId;

                sPcmFile1 = vPcmFiles[i * 2];

                sPcmFile2 = vPcmFiles[i * 2 + 1];
                sprintf(szCallId, "%s.%d", sCallId.c_str(), i);
                vThrds.push_back(std::thread(thrdCall, i, sSvrIp, nPort, nSleeptime, cFlag, std::string(szCallId), nUdpCount, nSampleRate, nChannelCount, nEncoding, sPcmFile1, sPcmFile2, nSendBlockSize));
                //currThrd = std::thread(thrdCall, sSvrIp, nPort, nSleeptime, cFlag, std::string(szCallId), nUdpCount, nSampleRate, nChannelCount, nEncoding, sPcmFile1, sPcmFile2, nSendBlockSize);
                //currThrd.detach();
                if (!strncmp(isloop, "loop", 4)) {
                    printf("DEBU\n");
                    vThrds[0].detach();
                    vThrds.clear();
                }
                increaseCallCount();

                //if (i >= (nThrdNum - 1)) i = 0;
                //else i++;
            }
#if 0
            else {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            //std::this_thread::sleep_for(std::chrono::seconds(1));
#if 0
            if (gTotalCall < nThrdNum)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            else
                std::this_thread::sleep_for(std::chrono::seconds(5));
#endif
            //vThrds[i].detach();
        }
#if 1
        if (strncmp(isloop, "loop", 4)) {
            printf("DEBU1\n");
            for (int i = 0; i < vThrds.size(); i++) {
                vThrds[i].join();
            }
        }

        if (!strncmp(isloop, "loop", 4)) {
            printf("DEBU2\n");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
#endif
    } while (!strncmp(isloop, "loop", 4));
    return 0;

    return 0;
}

