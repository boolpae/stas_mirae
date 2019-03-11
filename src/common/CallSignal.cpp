#ifdef  ENABLE_REALTIME

#ifdef WIN32

#include <WinSock2.h>

#else

#include <string.h>
#include <arpa/inet.h>

#undef htons
#undef ntohs

#endif

#include "CallSignal.h"
// #include "stas.h"

#include <string>

Protocol::CallSignal::CallSignal()
	: m_nPacketSize(0), m_Packet(NULL)
{
	// m_Logger = config->getLogger();
}


Protocol::CallSignal::~CallSignal()
{
	if (m_Packet) {
		delete[] m_Packet;
		m_nPacketSize = 0;
	}
}

void Protocol::CallSignal::init()
{
	if (m_Packet) {
		delete[] m_Packet;
		m_Packet = NULL;
		m_nPacketSize = 0;

	}
}

uint16_t Protocol::CallSignal::parsePacket(uint8_t * packet)
{
	uint8_t *pos = packet;
	uint16_t val;
    int32_t valRate;

#ifdef EN_RINGBACK_LEN
	uint32_t valRingbackLen=0;
#endif


	// 1. Check Valid-Protocol : RT-STT
	if (strncmp((const char*)packet, PROTO_AUTH_TOKEN, PROTO_AUTH_TOKEN_LEN)) {
		return uint16_t(404);
	}

	pos += PROTO_AUTH_TOKEN_LEN;

	// 2. Check Packet Length
	memcpy((void*)&val, pos, sizeof(uint16_t));
	this->pacSize = ::ntohs(val);

	// 3. Check Packet Flag : 'B', 'E'
	pos += sizeof(uint16_t);
	pacFlag = *pos;

	// 4. Check Call-ID
	pos += sizeof(uint8_t);
	memcpy(this->pacCounselorCode, pos, sizeof(this->pacCounselorCode)-1);
	this->pacCounselorCode[sizeof(this->pacCounselorCode)-1] = 0;
    for (int i=::strlen((const char*)this->pacCounselorCode); i>0; i--) {
        if (::isspace(this->pacCounselorCode[i])) {
            this->pacCounselorCode[i] = 0;
        }
    }

	pos += (sizeof(this->pacCounselorCode) - 1);
	memcpy(this->pacCallId, pos, sizeof(this->pacCallId)-1);
	this->pacCallId[sizeof(this->pacCallId)-1] = 0;
    for (int i=::strlen((const char*)this->pacCallId); i>0; i--) {
        if (::isspace(this->pacCallId[i])) {
            this->pacCallId[i] = 0;
        }
    }

	if (pacFlag == 'B') {
		// 5. Check UDP Count
		pos += (sizeof(this->pacCallId) - 1);
		this->pacUdpCnt = *pos;

		// 6. Check Sample Rate(Hz)
		pos += sizeof(uint8_t);
		memcpy((void*)&valRate, pos, sizeof(int32_t));
		this->pacSampleRate = ::ntohl(valRate);

		// 7. Check Channel Count
		pos += sizeof(int32_t);
		this->pacChnCnt = *pos;

		// 8. Check Sample Rate(Hz)
		pos += sizeof(uint8_t);
		this->pacEnc = *pos;
    
		pos += sizeof(uint8_t);
	}
	else {	// 호 종료 패킷인 경우 CH Count, Playtime 필드가 없으므로 바로 Fingerprint 필드로 넘어감
		pos += (sizeof(this->pacCallId) - 1);
	}

	// 9. Check Finger-Print
	memcpy(this->pacFingerPrint, pos, sizeof(this->pacFingerPrint) - 1);
	this->pacFingerPrint[sizeof(this->pacFingerPrint)-1] = 0;

#ifdef EN_RINGBACK_LEN	// 통화 시작부터 통화 실제 연결까지 시간 값을 가져옴
	if (pacFlag == 'B') {
		pos += 64;//sizeof(uint32_t);
		memcpy((void*)&valRingbackLen, pos, sizeof(uint32_t));
		this->pacRingbackLen = ::ntohl(valRingbackLen);
	}
#endif

	return uint16_t(200);
}


int16_t Protocol::CallSignal::makePacket(uint8_t flag)
{
	uint8_t *pos = NULL;
	uint16_t pSize;
    int32_t rate;

#ifdef EN_RINGBACK_LEN
	uint32_t ringbackLen;
#endif

	init();

	// flag == 'B' 호 시작 요청 패킷 생성
	if (flag == 'B') {
		pos = m_Packet = new uint8_t[CALL_BEG_PACKET_LEN+1];
        pacSize = uint16_t(CALL_BEG_PACKET_BODY_LEN);
		pSize = htons(pacSize);
		m_nPacketSize = uint16_t(CALL_BEG_PACKET_LEN);
        memset( m_Packet, ' ', CALL_BEG_PACKET_LEN);
        m_Packet[CALL_BEG_PACKET_LEN] = 0;
	}
	// flag == 'E' 호 종료 요청 패킷 생성
	else if (flag == 'E') {
		pos = m_Packet = new uint8_t[CALL_END_PACKET_LEN+1];
        pacSize = uint16_t(CALL_END_PACKET_BODY_LEN);
		pSize = htons(pacSize);
		m_nPacketSize = uint16_t(CALL_END_PACKET_LEN);
        memset( m_Packet, ' ', CALL_END_PACKET_LEN);
        m_Packet[CALL_END_PACKET_LEN] = 0;
	}

	memcpy(pos, PROTO_AUTH_TOKEN, PROTO_AUTH_TOKEN_LEN);
    
	pos += PROTO_AUTH_TOKEN_LEN;
	memcpy(pos, &pSize, sizeof(uint16_t));
	
    pos += sizeof(uint16_t);
	memcpy(pos, &flag, 1);
	
    pos += sizeof(uint8_t);
	memcpy(pos, pacCounselorCode, ::strlen((const char*)pacCounselorCode));
	
    pos += (sizeof(pacCounselorCode) - 1);

	memcpy(pos, pacCallId, ::strlen((const char*)pacCallId));
	
    pos += (sizeof(pacCallId) - 1);

	if (flag == 'B') {
		memcpy(pos, &pacUdpCnt, sizeof(uint8_t));
		pos += sizeof(uint8_t);
        rate = ::htonl(pacSampleRate);
		memcpy(pos, &rate, sizeof(int32_t));
		pos += sizeof(int32_t);
        memcpy(pos, &pacChnCnt, 1);
		pos += sizeof(uint8_t);
        memcpy(pos, &pacEnc, 1);
        pos += sizeof(uint8_t);
	}

	memcpy(pos, pacFingerPrint, ::strlen((const char*)pacFingerPrint));

#ifdef EN_RINGBACK_LEN
	if (flag == 'B') {
		pos += 64;
		ringbackLen = ::htonl(pacRingbackLen);
		memcpy(pos, &ringbackLen, sizeof(uint32_t));
	}
#endif

	return int16_t(0);
}

int16_t Protocol::CallSignal::makePacket(uint8_t * packet, uint16_t packetlen)
{
	init();

	m_Packet = new uint8_t[packetlen+1];
	m_nPacketSize = packetlen;

	memcpy(m_Packet, packet, packetlen);
	m_Packet[packetlen] = 0;

	return int16_t(0);
}

int16_t Protocol::CallSignal::makePacket(uint8_t* packet, uint16_t packetlen, uint16_t rescode)
{
	uint16_t pacSize = 0;

	init();

	m_nPacketSize = packetlen + (sizeof(uint8_t) * 3);
	m_Packet = new uint8_t[m_nPacketSize+1];

	memcpy(m_Packet, packet, packetlen);
	memcpy(m_Packet + packetlen, std::to_string((unsigned int)rescode).c_str(), (sizeof(uint8_t) * 3));
	m_Packet[m_nPacketSize] = 0;


	pacSize = ::htons(m_nPacketSize-8);
	memcpy(m_Packet+PROTO_AUTH_TOKEN_LEN, &pacSize, sizeof(uint16_t));

	return int16_t(0);
}

int16_t Protocol::CallSignal::makePacket(uint8_t* packet, uint16_t packetlen, std::vector< uint16_t > &vPorts)
{
	uint16_t u16val = 0;
	std::vector< uint16_t >::iterator iter;
	uint8_t *pos;
	uint16_t pacSize = 0;

	init();

	m_nPacketSize = packetlen + (uint16_t)(sizeof(uint8_t) * 3) + (uint16_t)(sizeof(uint16_t) * vPorts.size());
	pos = m_Packet = new uint8_t[m_nPacketSize+1];

	memcpy(pos, packet, packetlen);
	pos += packetlen;
	memcpy(pos, "200", (sizeof(uint8_t) * 3));
	pos += (sizeof(uint8_t) * 3);

	for (iter = vPorts.begin(); iter != vPorts.end(); iter++) {
		u16val = ::htons((*iter));
		memcpy(pos, &u16val, sizeof(uint16_t));
		pos += sizeof(uint16_t);
	}
	m_Packet[m_nPacketSize] = 0;

	pacSize = ::htons(m_nPacketSize-8);
	memcpy(m_Packet + PROTO_AUTH_TOKEN_LEN, &pacSize, sizeof(uint16_t));

	return int16_t(0);
}

void Protocol::CallSignal::setPacCounselorCode(uint8_t * counselorcode, uint16_t len)
{
	memset(pacCounselorCode, 0x00, sizeof(pacCounselorCode));
	if (len > 32) len = 32;
	memcpy(pacCounselorCode, counselorcode, len);
    // m_Logger->debug
	// fprintf(stderr, "CallSignal::pacCounselorCode() - Call-ID(%s : %s), Len(%d)\n", counselorcode, pacCounselorCode, len);
}

void Protocol::CallSignal::setPacCallId(uint8_t * callid, uint16_t len)
{
	memset(pacCallId, 0x00, sizeof(pacCallId));
	if (len > LEN_CALL_ID) len = LEN_CALL_ID;
	memcpy(pacCallId, callid, len);
    // m_Logger->debug("CallSignal::setPacCallId() - Call-ID(%s : %s), Len(%d)", callid, pacCallId, len);
	// fprintf(stderr, "CallSignal::setPacCallId() - Call-ID(%s : %s), Len(%d)\n", callid, pacCallId, len);
}

void Protocol::CallSignal::setFingerPrint(uint8_t * fprint, uint16_t len)
{
	memset(pacFingerPrint, 0x00, sizeof(pacFingerPrint));
	pacFingerPrint[sizeof(pacFingerPrint)-1] = 0;
	if (len > 64) len = 64;
	memcpy(pacFingerPrint, fprint, len);
    // m_Logger->debug("CallSignal::setFingerPrint() - FingerPrint(%s : %s), Len(%d)", fprint, pacFingerPrint, len);
	// fprintf(stderr, "CallSignal::setFingerPrint() - FingerPrint(%s : %s), Len(%d)\n", fprint, pacFingerPrint, len);
}

void Protocol::CallSignal::printPacketInfo()
{
	//m_Logger->debug("\n  **---- Packet Info ----**\n"
	fprintf(stderr, "\n  **---- Packet Info ----**\n"
	"  Packet Size : %d\n"
	"  Flag : %c\n"
	"  Counselor-Code(%lu) : [%s]\n"
	"  Call-ID(%lu) : [%s]\n"
	"  UDP Count : %d\n"
	"  SampleRate : %d\n"
	"  Channel Count : %d\n"
	"  Encoding : %d\n"
	"  Fingerprint(%lu) : [%s]"
    , pacSize, pacFlag, sizeof(pacCounselorCode), pacCounselorCode, sizeof(pacCallId), pacCallId, pacUdpCnt, pacSampleRate, pacChnCnt, pacEnc, sizeof(pacFingerPrint), pacFingerPrint);

}


#endif // ENABLE_REALTIME
