#ifdef  ENABLE_REALTIME
#ifdef USE_REALTIME_MT

#include "VRClientMT.h"
#include "VRCManager.h"
#include "WorkTracer.h"
#include "FileHandler.h"

#ifndef USE_ODBC
#include "DBHandler.h"
#else
#include "DBHandler_ODBC.h"
#endif

#include "HAManager.h"
#include "stas.h"

#include <thread>
#include <iostream>
#include <fstream>

#include <string.h>

#include <boost/algorithm/string/replace.hpp>

// For Gearman
#include <libgearman/gearman.h>

#ifdef FAD_FUNC

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <sys/mman.h>

#include <string.h>
#include <stdlib.h>

#include <fvad.h>

#ifdef USE_REDIS_POOL
#include "RedisHandler.h"

#include <iconv.h>

#include "rapidjson/document.h"     // rapidjson's DOM-style API
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#endif

#include "Utils.h"

#define WAVE_FORMAT_UNKNOWN      0X0000;
#define WAVE_FORMAT_PCM          0X0001;
#define WAVE_FORMAT_MS_ADPCM     0X0002;
#define WAVE_FORMAT_IEEE_FLOAT   0X0003;
#define WAVE_FORMAT_ALAW         0X0006;
#define WAVE_FORMAT_MULAW        0X0007;
#define WAVE_FORMAT_IMA_ADPCM    0X0011;
#define WAVE_FORMAT_YAMAHA_ADPCM 0X0016;
#define WAVE_FORMAT_GSM          0X0031;
#define WAVE_FORMAT_ITU_ADPCM    0X0040;
#define WAVE_FORMAT_MPEG         0X0050;
#define WAVE_FORMAT_EXTENSIBLE   0XFFFE;

//#define CHANNEL_SYNC    // 두 화자 간의 음성 데이터 처리 속도를 맞추기 위한 정의
//#define DIAL_SYNC       // 두 화자 간의 대화 내용을 맞추기 위한 정의(CHANNEL_SYNC가 이미 정의되어야 한다.)

typedef struct
{
	unsigned char ChunkID[4];    // Contains the letters "RIFF" in ASCII form
	unsigned int ChunkSize;      // This is the size of the rest of the chunk following this number
	unsigned char Format[4];     // Contains the letters "WAVE" in ASCII form
} RIFF;

//-------------------------------------------
// [Channel]
// - streo     : [left][right]
// - 3 channel : [left][right][center]
// - quad      : [front left][front right][rear left][reat right]
// - 4 channel : [left][center][right][surround]
// - 6 channel : [left center][left][center][right center][right][surround]
//-------------------------------------------
typedef struct
{
	unsigned char  ChunkID[4];    // Contains the letters "fmt " in ASCII form
	unsigned int   ChunkSize;     // 16 for PCM.  This is the size of the rest of the Subchunk which follows this number.
	unsigned short AudioFormat;   // PCM = 1
	unsigned short NumChannels;   // Mono = 1, Stereo = 2, etc.
	unsigned int   SampleRate;    // 8000, 44100, etc.
	unsigned int   AvgByteRate;   // SampleRate * NumChannels * BitsPerSample/8
	unsigned short BlockAlign;    // NumChannels * BitsPerSample/8
	unsigned short BitPerSample;  // 8 bits = 8, 16 bits = 16, etc
} FMT;


typedef struct
{
	char          ChunkID[4];    // Contains the letters "data" in ASCII form
	unsigned int  ChunkSize;     // NumSamples * NumChannels * BitsPerSample/8
} DATA;


typedef struct
{
	RIFF Riff;
	FMT	 Fmt;
	DATA Data;
} WAVE_HEADER;

#endif // FAD_FUNC

#ifdef USE_REDIS_POOL
static unsigned int APHash(const char *str) {
    unsigned int hash = 0;
    int i;
    for (i=0; *str; i++) {
        if ((i&  1) == 0) {
            hash ^= ((hash << 7) ^ (*str++) ^ (hash >> 3));
        } else {
            hash ^= (~((hash << 11) ^ (*str++) ^ (hash >> 5)));
        }
    }
    return (hash&  0x7FFFFFFF);
}

enum {
 CACHE_TYPE_1, 
 CACHE_TYPE_2,
 CACHE_TYPE_MAX,
};
#endif

CtrlThreadInfo::CtrlThreadInfo()
: ServerName("DEFAULT"), TotalVoiceDataLen(0), DiaNumber(0), RxState(1), TxState(1)
{

}
uint32_t CtrlThreadInfo::getDiaNumber()
{
    std::lock_guard<std::mutex> g(m_mxDianum);
    return ++DiaNumber;
}

// std::map<std::string, std::shared_ptr<CtrlThreadInfo>> VRClient::ThreadInfoTable;

#ifdef EN_RINGBACK_LEN
VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT, uint32_t ringbacklen)
#else
VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT)
#endif
	: m_sGearHost(gearHost), m_nGearPort(gearPort), m_nGearTimeout(gearTimeout), m_sFname(fname), m_sCallId(callid), m_sCounselCode(counselcode), m_nLiveFlag(1), m_cJobType(jobType), m_nNumofChannel(noc), m_deliver(deliver), m_s2d(s2d), m_is_save_pcm(is_save_pcm), m_pcm_path(pcm_path), m_framelen(framelen*8), m_mode(mode)
{
    std::shared_ptr<CtrlThreadInfo> thrdInfo = std::make_shared<CtrlThreadInfo>();

    // ThreadInfoTable[m_sCallId] = thrdInfo;

	m_Mgr = mgr;

    rx_sframe=0;
    tx_sframe=0;
#ifdef EN_RINGBACK_LEN
    rx_eframe=ringbacklen;
    tx_eframe=ringbacklen;
#else
    rx_eframe=0;
    tx_eframe=0;
#endif
    syncBreak = 0;

    rx_hold = 0;
    tx_hold = 0;
    //thrd.detach();
	//printf("\t[DEBUG] VRClinetMT Constructed.\n");

    m_tStart = startT;
#ifdef EN_RINGBACK_LEN
    m_nRingbackLen = ringbacklen;
#endif

	m_thrd = std::thread(VRClient::thrdMain, this);
    m_thrd.detach();
	m_thrdRx = std::thread(VRClient::thrdRxProcess, this);
    m_thrdRx.detach();
	m_thrdTx = std::thread(VRClient::thrdTxProcess, this);
    m_thrdTx.detach();

    m_Logger = config->getLogger();
    m_Logger->debug("VRClinetMT Constructed.");
}


VRClient::~VRClient()
{
	QueItem* item;
	// while (!m_qRTQue.empty()) {
	// 	item = m_qRTQue.front();
	// 	m_qRTQue.pop();

	// 	delete[] item->voiceData;
	// 	delete item;
	// }

	while (!m_qRXQue.empty()) {
		item = m_qRXQue.front();
		m_qRXQue.pop();

		delete[] item->voiceData;
		delete item;
	}

	while (!m_qTXQue.empty()) {
		item = m_qTXQue.front();
		m_qTXQue.pop();

		delete[] item->voiceData;
		delete item;
	}

	//printf("\t[DEBUG] VRClinetMT Destructed.\n");
    // ThreadInfoTable.erase(ThreadInfoTable.find(m_sCallId));
    // m_Logger->debug("VRClinetMT Destructed. TableSize(%d)", ThreadInfoTable.size());
    m_Logger->debug("VRClinetMT Destructed. CALLID(%s), CS_CD(%s)", m_sCallId.c_str(), m_sCounselCode.c_str());
}

void VRClient::finish()
{
	m_nLiveFlag = 0;
}

#define BUFLEN (16000 * 10 + 64)  //Max length of buffer

typedef struct _posPair {
    uint64_t bpos;
    uint64_t epos;
} PosPair;

#define WAV_HEADER_SIZE 44
#define WAV_BUFF_SIZE 19200
#define MM_SIZE (WAV_HEADER_SIZE + WAV_BUFF_SIZE)

void VRClient::thrdMain(VRClient* client) {
    char timebuff [32];
    struct tm * timeinfo;
    std::string sPubCannel = config->getConfig("redis.pubchannel", "RT-STT");
    bool useDelCallInfo = !config->getConfig("stas.use_del_callinfo", "false").compare("true");
    int nDelSecs = config->getConfig("stas.del_secs", 0);

#ifdef USE_REDIS_POOL
    int64_t zCount=0;
    std::string redisKey = "G_CS:";
    char redisValue[256];
    std::string strRedisValue;
    bool useRedis = (!config->getConfig("redis.use", "false").compare("true") & !config->getConfig("redis.send_rt_stt", "false").compare("true"));
    xRedisClient &xRedis = client->getXRdedisClient();
    RedisDBIdx dbi(&xRedis);
    VALUES vVal;
#endif

    // auto search = client->ThreadInfoTable[client->m_sCallId];

#ifdef USE_REDIS_POOL
    if ( useRedis ) 
    {
        dbi.CreateDBIndex(client->getCallId().c_str(), APHash, CACHE_TYPE_1);

        // 2019-01-10, 호 시작 시 상담원 상태 변경 전달 - 호 시작
        zCount=0;
        redisKey.append(client->getCounselCode());

        //  {"REG_DTM":"10:15", "STATE":"E", "CALL_ID":"CALL011"}
        timeinfo = localtime(&client->m_tStart);
        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",timeinfo);
        sprintf(redisValue, "{\"REG_DTM\":\"%s\", \"STATE\":\"I\", \"CALL_ID\":\"%s\"}", timebuff, client->getCallId().c_str());
        strRedisValue = redisValue;
        
        // xRedis.hset( dbi, redisKey, client->getCallId(), strRedisValue, zCount );
        vVal.push_back( strRedisValue );
        xRedis.lpush( dbi, redisKey, vVal, zCount );
        vVal.clear();

        redisKey = "G_RTSTT:";
        redisKey.append(client->getCallId());
    }
#endif

    auto t1 = std::chrono::high_resolution_clock::now();
      
    while (client->thrdInfo.getRxState() || client->thrdInfo.getTxState())// client->m_RxState || client->m_TxState)
    {
        // client->m_Logger->debug("VRClient::thrdMain(%s) - RxState(%d), TxState(%d)", client->m_sCallId.c_str(), search->getRxState(), search->getTxState());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));//microseconds(10));//seconds(1));
    }
    
    uint64_t totalVLen = client->thrdInfo.getTotalVoiceDataLen();//client->getTotalVoiceDataLen();
    std::string svr_nm = client->thrdInfo.getServerName();//client->getServerName();
    client->m_Logger->debug("VRClient::thrdMain(%s) - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->thrdInfo.getServerName().c_str(), client->thrdInfo.getTotalVoiceDataLen());

    client->m_Mgr->removeVRC(client->m_sCallId);

#ifdef USE_REDIS_POOL
    if ( useRedis ) {
        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = localtime(&t);

        if ( !xRedis.publish(dbi, sPubCannel.c_str(), client->getCallId().c_str(), zCount) )
            client->m_Logger->error("VRClient::thrdMain(%s) - redis publish(). [%s], zCount(%d)", client->m_sCallId.c_str(), dbi.GetErrInfo(), zCount);

        redisKey = "G_CS:";
        redisKey.append(client->getCounselCode());

        //  {"REG_DTM":"10:15", "STATE":"E", "CALL_ID":"CALL011"}
        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",tmp);
        sprintf(redisValue, "{\"REG_DTM\":\"%s\", \"STATE\":\"E\", \"CALL_ID\":\"%s\"}", timebuff, client->getCallId().c_str());
        strRedisValue = redisValue;
        
        // xRedis.hset( dbi, redisKey, client->getCallId(), strRedisValue, zCount );
        xRedis.lset(dbi, redisKey, 0, strRedisValue);

    }
#endif

    if (client->m_s2d) {
        auto t2 = std::chrono::high_resolution_clock::now();
        timeinfo = localtime(&client->m_tStart);
        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",timeinfo);

        client->m_s2d->updateCallInfo(client->m_sCallId, true);
        if ( !useDelCallInfo || ( totalVLen/16000 > nDelSecs ) ) {
            client->m_s2d->updateTaskInfo(client->m_sCallId, std::string(timebuff), std::string("MN"), client->m_sCounselCode, 'Y', totalVLen, totalVLen/16000, std::chrono::duration_cast<std::chrono::seconds>(t2-t1).count(), 0, "TBL_JOB_INFO", "", svr_nm.c_str());
        }
    }

    // HA
    if (HAManager::getInstance())
#ifdef EN_RINGBACK_LEN
        HAManager::getInstance()->insertSyncItem(false, client->m_sCallId, client->m_sCounselCode, std::string("remove"), 1, 1, 0);
#else
        HAManager::getInstance()->insertSyncItem(false, client->m_sCallId, client->m_sCounselCode, std::string("remove"), 1, 1);
#endif

    if (client->m_is_save_pcm) {
        if (config->isSet("stas.merge")) {
            std::string cmd = "";
            cmd = config->getConfig("stas.merge");
            cmd.push_back(' ');
            cmd.append(client->m_pcm_path.c_str());
            cmd.push_back(' ');
            cmd.append(client->m_sCallId.c_str());
            // job_log->debug("[%s, 0x%X] %s", job_name, THREAD_ID, cmd.c_str());
            if (std::system(cmd.c_str())) {
                client->m_Logger->error("VRClient::thrdMain(%s) Fail to merge wavs: command(%s)", client->m_sCallId.c_str(), cmd.c_str());
            }
        }
    }
    client->m_Logger->debug("VRClient::thrdMain(%s) - FINISH CALL.", client->m_sCallId.c_str());

	WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

    // 3초 이하 호 정보 삭제 - totalVLen/16000 < 3 인경우 호 정보 삭제
    if ( useDelCallInfo && nDelSecs ) {
        if ( useRedis && (totalVLen/16000 <= nDelSecs) ) {
            redisKey = "G_CS:";
            redisKey.append(client->m_sCounselCode);
            xRedis.lpop(dbi, redisKey, strRedisValue);

            redisKey = "G_RTSTT:";
            redisKey.append(client->m_sCallId);
            xRedis.del(dbi, redisKey);

        }
        if ( client->m_s2d && (totalVLen/16000 <= nDelSecs) ) {
            client->m_s2d->deleteJobInfo(client->m_sCallId);
        }
    }

	// client->m_thrd.detach();
	delete client;
}

void VRClient::thrdRxProcess(VRClient* client) {

	QueItem* item;
    gearman_client_st *gearClient;
    gearman_return_t ret;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;
    PosPair stPos;
    WAVE_HEADER wHdr;
    
    char buf[BUFLEN];
    uint16_t nHeadLen=0;
    
    uint8_t *vpBuf = NULL;
    size_t posBuf = 0;
    std::vector<uint8_t> vBuff;
#ifdef DIAL_SYNC_N_BUFF_CTRL
    std::vector<uint8_t> vTempBuff;
    std::vector<uint8_t>::iterator vIter;
#endif
    uint64_t totalVoiceDataLen;
    size_t framelen;
    std::string svr_nm;
    char fname[64];

    std::string fhCallId;
    char timebuff [32];
    char datebuff[32];
    struct tm * timeinfo = localtime(&client->m_tStart);
    strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",timeinfo);
    strftime (datebuff,sizeof(datebuff),"%Y%m%d",timeinfo);
    std::string fullpath = client->m_pcm_path + "/" + datebuff + "/" + client->m_sCounselCode + "/";
    std::string filename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + std::string("_r.wav");
    std::ofstream pcmFile;
    bool bOnlyRecord = !config->getConfig("stas.only_record", "false").compare("true");
    int nMinVBuffSize = config->getConfig("stas.min_buff_size", 10000);
    int nMaxWaitNo = config->getConfig("stas.max_wait_no", 7);
    int nCurrWaitNo = 0;
#ifdef EN_SAVE_PCM
    bool bUseSavePcm = !config->getConfig("stas.use_save_pcm", "false").compare("true");
    std::string pcmFilename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
#endif
    // auto search = client->ThreadInfoTable[client->m_sCallId];

    vBuff.reserve(MM_SIZE);

    MakeDirectory(fullpath.c_str());
    
    framelen = client->m_framelen * 2;

    sprintf(fname, "%s", client->m_sFname.c_str());

    fhCallId = std::string(timebuff) + "_" + client->m_sCallId;

#ifdef USE_REDIS_POOL
    bool useRedis = (!config->getConfig("redis.use", "false").compare("true") & !config->getConfig("redis.send_rt_stt", "false").compare("true"));
    iconv_t it;
    VALUES vVal;
    std::string sPubCannel = config->getConfig("redis.pubchannel", "RT-STT");
    xRedisClient &xRedis = client->getXRdedisClient();
    RedisDBIdx dbi(&xRedis);
    std::string redisKey = "G_RTSTT:";

    if ( useRedis ) {
        dbi.CreateDBIndex(client->getCallId().c_str(), APHash, CACHE_TYPE_1);
        it = iconv_open("UTF-8", "EUC-KR");
    }

    redisKey.append(client->getCallId());
#endif

    memcpy(wHdr.Riff.ChunkID, "RIFF", 4);
    wHdr.Riff.ChunkSize = 0;
    memcpy(wHdr.Riff.Format, "WAVE", 4);

    memcpy(wHdr.Fmt.ChunkID, "fmt ", 4);
    wHdr.Fmt.ChunkSize = 16;
    wHdr.Fmt.AudioFormat = 1;
    wHdr.Fmt.NumChannels = 1;
    wHdr.Fmt.SampleRate = 8000;
    wHdr.Fmt.AvgByteRate = 8000 * 1 * 16 / 8 ;
    wHdr.Fmt.BlockAlign = 1 * 16 / 8;
    wHdr.Fmt.BitPerSample = 16;

    memcpy(wHdr.Data.ChunkID, "data", 4);
    wHdr.Data.ChunkSize = 0;

    if (client->m_is_save_pcm) {
        // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_r.wav");
        // std::ofstream pcmFile;

        pcmFile.open(filename, ios::out | ios::trunc | ios::binary);
        if (pcmFile.is_open()) {
            pcmFile.write((const char*)&wHdr, sizeof(WAVE_HEADER));
            pcmFile.close();
        }
    }
    
    stPos.bpos = 0;
    stPos.epos = 0;
    
    
    if ( !bOnlyRecord ) {

    gearClient = gearman_client_create(NULL);
    if (!gearClient) {
        //printf("\t[DEBUG] VRClient::thrdRxProcess() - ERROR (Failed gearman_client_create - %s)\n", client->m_sCallId.c_str());
        client->m_Logger->error("VRClient::thrdRxProcess() - ERROR (Failed gearman_client_create - %s)", client->m_sCallId.c_str());

        // WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        client->thrdInfo.setRxState(0);//client->m_RxState = 0;
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // client->m_thrdRx.detach();
        // delete client;
        return;
    }
    
    ret= gearman_client_add_server(gearClient, client->m_sGearHost.c_str(), client->m_nGearPort);
    if (gearman_failed(ret))
    {
        //printf("\t[DEBUG] VRClient::thrdRxProcess() - ERROR (Failed gearman_client_add_server - %s)\n", client->m_sCallId.c_str());
        client->m_Logger->error("VRClient::thrdRxProcess() - ERROR (Failed gearman_client_add_server - %s)", client->m_sCallId.c_str());

        // WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        client->thrdInfo.setRxState(0);// client->m_RxState = 0;
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // client->m_thrdRx.detach();
        // delete client;
        return;
    }

    } // only_record
    

	// m_cJobType에 따라 작업 형태를 달리해야 한다. 
	if (client->m_cJobType == 'R') {
        uint32_t diaNumber=1;   // DB 실시간 STT 테이블에 저장될 호(Call)단위 Index 값
        Fvad *vad = NULL;
        int vadres, before_vadres;
        int aDianum;

        vad = fvad_new();
        if (!vad) {//} || (fvad_set_sample_rate(vad, in_info.samplerate) < 0)) {
            client->m_Logger->error("VRClient::thrdRxProcess() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
            if ( !bOnlyRecord ) gearman_client_free(gearClient);
            // WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);
            client->thrdInfo.setRxState(0);// client->m_RxState = 0;
            // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // client->m_thrdRx.detach();
            // delete client;
            return;
        }
        fvad_set_sample_rate(vad, 8000);
        fvad_set_mode(vad, client->m_mode);

		// 실시간의 경우 통화가 종료되기 전까지 Queue에서 입력 데이터를 받아 처리
		// FILE인 경우 기존과 동일하게 filename을 전달하는 방법 이용
        if (client->m_nGearTimeout) {
            if ( !bOnlyRecord ) gearman_client_set_timeout(gearClient, client->m_nGearTimeout);
        }
        
#if 0 // for DEBUG
		std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_") + std::to_string(client->m_nNumofChannel) + std::string(".pcm");
		std::ofstream pcmFile;
        if (client->m_is_save_pcm)
            pcmFile.open(filename, ios::out | ios::app | ios::binary);
#endif

        // write wav heaer to file(mmap);
        vBuff.clear();
        client->rx_sframe = 0;
        // client->rx_eframe = 0;
        client->rx_hold = 0;
        aDianum = 0;
        totalVoiceDataLen = 0;
        svr_nm = "DEFAULT";

        vadres = before_vadres = 0;
		while (client->thrdInfo.getRxState())//(client->m_nLiveFlag)
		{
			while (!client->m_qRXQue.empty()) {
				// g = new std::lock_guard<std::mutex>(client->m_mxQue);
				item = client->m_qRXQue.front();
				client->m_qRXQue.pop();
				// delete g;

                totalVoiceDataLen += item->lenVoiceData;

                stPos.epos += item->lenVoiceData;
				// queue에서 가져온 item을 STT 하는 로직을 아래에 코딩한다.
				// call-id + item->spkNo => call-id for rt-stt
                memset(buf, 0, sizeof(buf));
                if (!item->flag) {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "LAST");
                }
                else if (item->flag == 2) {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "FIRS");
                }
                else {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "NOLA");
                }
                nHeadLen = strlen(buf);

                if (vBuff.size()>0) {
                    for(int i=0; i<nHeadLen; i++) {
                        vBuff[i] = buf[i];
                    }
                }
                else {
                    for(int i=0; i<nHeadLen; i++) {
                        vBuff.push_back(buf[i]);
                    }
                }
                

                if (client->m_is_save_pcm) {
                    // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_r.wav");
                    // std::ofstream pcmFile;

                    pcmFile.open(filename, ios::out | ios::app | ios::binary);
                    if (pcmFile.is_open()) {
                        pcmFile.write((const char*)item->voiceData, item->lenVoiceData);
                        pcmFile.close();
                    }
                }
                
                if ( !bOnlyRecord ) {
                // check vad!, by loop()
                // if finish check vad and vBuff is no empty, send buff to VR by gearman
                // vadres == 1 vBuff[item->spkNo-1].push_back();
                // vadres == 0 and vBuff[item->spkNo-1].size() > 0 then send buff to gearman
                posBuf = 0;
                while ((item->lenVoiceData >= framelen) && ((item->lenVoiceData - posBuf) >= framelen)) {
                    vpBuf = (uint8_t *)(item->voiceData+posBuf);
#ifdef CHANNEL_SYNC
                    // for channel sync
                    while (client->tx_eframe < client->rx_eframe) {
                        if ( 
                            client->syncBreak ||
#ifdef DIAL_SYNC
                            client->tx_hold || 
#endif
                            !item->flag) break;
                        // client->m_Logger->error("VRClient::thrdRxProcess syncBreak(%d), tx_hold(%d), flag(%d) (%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", client->syncBreak, client->tx_hold, item->flag, client->m_sCallId.c_str());
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
#endif
                    client->rx_eframe += (client->m_framelen/8);

                    // Convert the read samples to int16
                    vadres = fvad_process(vad, (const int16_t *)vpBuf, client->m_framelen);

                    if (vadres < 0) {
                        //client->m_Logger->error("VRClient::thrdMain(%d, %d, %s)(%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", nHeadLen, item->spkNo, buf, client->m_sCallId.c_str(), vBuff[item->spkNo-1].size(), client->rx_sframe[item->spkNo-1], client->rx_eframe[item->spkNo-1]);
                        continue;
                    }

                    if (vadres > 0) {
                        for(size_t i=0; i<framelen; i++) {
                            vBuff.push_back(vpBuf[i]);
                            
                        }
                    }
                    
                    if (!vadres && (vBuff.size()<=nHeadLen)) {
                        // start ms
                        client->rx_sframe = client->rx_eframe - (client->m_framelen/8);//20;
                    }
#ifdef DEBUGING
                    if (vadres && !before_vadres) {
                        // 음성 시작 점 - Voice Active Detection Poing
                        std::string filename = client->m_sCallId + std::string("_vadpoint_r.txt");
                        std::ofstream pcmFile;

                        pcmFile.open(filename, ios::out | ios::app);
                        if (pcmFile.is_open()) {
                            pcmFile << client->rx_sframe << " ";
                            pcmFile.close();
                        }
                    }

                    if (!vadres && before_vadres) {
                        // 음성 시작 점 - Voice Active Detection Poing
                        std::string filename = client->m_sCallId + std::string("_vadpoint_r.txt");
                        std::ofstream pcmFile;

                        pcmFile.open(filename, ios::out | ios::app);
                        if (pcmFile.is_open()) {
                            pcmFile << client->rx_eframe << std::endl;
                            pcmFile.close();
                        }
                    }
#endif
                    if (
                        client->tx_hold ||
#ifdef DIAL_SYNC_N_BUFF_CTRL
                        (client->tx_hold && (client->rx_sframe < client->tx_sframe)) || 
#endif
                        (!vadres && (vBuff.size()>nHeadLen))) {
#ifdef DIAL_SYNC_N_BUFF_CTRL
                        vTempBuff.clear();
                        if ((vBuff.size() > 15000) && client->tx_hold && (client->rx_sframe < client->tx_sframe)) {
                            size_t offset = (((client->tx_sframe - client->rx_sframe) * 16) + nHeadLen);
                            vTempBuff.assign(vBuff.begin() + offset, vBuff.end());
                            vBuff.erase(vBuff.begin() + offset, vBuff.end());
                        }
#endif
                        if ( (nCurrWaitNo > nMaxWaitNo) || (vBuff.size() > nMinVBuffSize) ) {   // 8000 bytes, 0.5 이하의 음성데이터는 처리하지 않음
#if 0 // VR로 데이터처리 요청 시 처리할 데이터의 sframe, eframe, buff.size 출력
                            if (1) {
                                // 음성 시작 점 - Voice Active Detection Poing
                                std::string filename = client->m_sCallId + std::string("_vadpoint_r.txt");
                                std::ofstream pcmFile;

                                pcmFile.open(filename, ios::out | ios::app);
                                if (pcmFile.is_open()) {
                                    pcmFile << client->rx_sframe << " " << client->rx_eframe << " " << vBuff.size()-nHeadLen << std::endl;
                                    pcmFile.close();
                                }
                            }
#endif

#ifdef DIAL_SYNC
                            if (!client->tx_hold) {
                                client->rx_hold = 1;
                                while (client->rx_hold) {
                                    if (!item->flag) break;
                                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                            }
#endif
                            // send buff to gearman
                            if (aDianum == 0) {
                                sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "FIRS");
                            }
                            else {
                                sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "NOLA");
                            }
                            for(size_t i=0; i<strlen(buf); i++) {
                                vBuff[i] = buf[i];
                            }
                            //client->m_Logger->debug("VRClient::thrdMain(%d, %d, %s)(%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", nHeadLen, item->spkNo, buf, client->m_sCallId.c_str(), vBuff[item->spkNo-1].size(), client->rx_sframe[item->spkNo-1], client->rx_eframe[item->spkNo-1]);

                            // auto d1 = std::chrono::high_resolution_clock::now();
                            #ifdef EN_SAVE_PCM
                            if (bUseSavePcm)
                            {
                                FILE *pPcm;

                                std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_r.pcm");
                                pPcm = fopen(tempPcmFile.c_str(), "wb");
                                if (pPcm)
                                {
                                    fwrite((const void*)&vBuff[0], sizeof(char), vBuff.size(), pPcm);
                                    fclose(pPcm);
                                }
                            }
                            #endif
                            value= gearman_client_do(gearClient, fname/*"vr_realtime"*/, NULL, 
                                                            (const void*)&vBuff[0], vBuff.size(),
                                                            &result_size, &rc);
                                                            
                            aDianum++;
                            // auto d2 = std::chrono::high_resolution_clock::now();
                            // client->m_Logger->debug("VRClient::thrdRxProcess(%s) - stt working msecs(%d)", client->m_sCallId.c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(d2-d1).count());
                            
                            if (gearman_success(rc))
                            {
                                // Make use of value
                                if (value) {
                                    std::string modValue = boost::replace_all_copy(std::string((const char*)value), "\n", " ");
                                    // std::cout << "DEBUG : value(" << (char *)value << ") : size(" << result_size << ")" << std::endl;
                                    //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);
                                    diaNumber = client->thrdInfo.getDiaNumber();//client->getDiaNumber();
#ifdef USE_REDIS_POOL
                                    if ( useRedis ) {
                                        int64_t zCount=0;
                                        std::string sJsonValue;

                                        size_t in_size, out_size;
                                        // iconv_t it;
                                        char *utf_buf = NULL;
                                        char *input_buf_ptr = NULL;
                                        char *output_buf_ptr = NULL;

                                        in_size = modValue.size();
                                        out_size = in_size * 2 + 1;
                                        utf_buf = (char *)malloc(out_size);

                                        if (utf_buf) {
                                            memset(utf_buf, 0, out_size);

                                            input_buf_ptr = (char *)modValue.c_str();
                                            output_buf_ptr = utf_buf;

                                            // it = iconv_open("UTF-8", "EUC-KR");

                                            iconv(it, &input_buf_ptr, &in_size, &output_buf_ptr, &out_size);
                                            
                                            // iconv_close(it);
                                            

                                            {
                                                rapidjson::Document d;
                                                rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

                                                d.SetObject();
                                                d.AddMember("IDX", diaNumber, alloc);
                                                d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                                d.AddMember("SPK", rapidjson::Value("R", alloc).Move(), alloc);
                                                d.AddMember("POS_START", client->rx_sframe/10, alloc);
                                                d.AddMember("POS_END", client->rx_eframe/10, alloc);
                                                d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                                rapidjson::StringBuffer strbuf;
                                                rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                                d.Accept(writer);

                                                sJsonValue = strbuf.GetString();
                                            }

                                            vVal.push_back(toString(diaNumber));
                                            vVal.push_back(sJsonValue);

                                            if ( !xRedis.zadd(dbi, redisKey/*client->getCallId()*/, vVal, zCount) ) {
                                                client->m_Logger->error("VRClient::thrdRxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str()/*client->m_sCallId.c_str()*/, dbi.GetErrInfo(), zCount);
                                            }
                                            vVal.clear();

                                            free(utf_buf);
                                        }
                                    }
#endif

#ifdef DISABLE_ON_REALTIME
                                    // to DB
                                    if (client->m_s2d) {
                                        client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, client->rx_sframe/10, client->rx_eframe/10, modValue);
                                    }
#endif // DISABLE_ON_REALTIME

                                    //STTDeliver::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                                    // to STTDeliver(file)
                                    if (client->m_deliver) {
                                        client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue, item->spkNo, client->rx_sframe/10, client->rx_eframe/10, client->m_sCounselCode);
                                    }

                                    free(value);
                                    
                                }
                            }
                            else if (gearman_failed(rc)){
                                client->m_Logger->error("VRClient::thrdRxProcess(%s) - failed gearman_client_do(). [%lu : %lu], timeout(%d)", client->m_sCallId.c_str(), client->rx_sframe, client->rx_eframe, client->m_nGearTimeout);
                            }

                            // and clear buff, set msg header
                            vBuff.clear();

                            for(size_t i=0; i<nHeadLen; i++) {
                                //vBuff[item->spkNo-1][i] = buf[i];
                                vBuff.push_back(buf[i]);

                            }
                            client->rx_sframe = client->rx_eframe;

                            if (client->tx_hold) {
                                client->tx_hold = 0;
#ifdef DIAL_SYNC_N_BUFF_CTRL
                                for(vIter=vTempBuff.begin(); vIter!=vTempBuff.end(); vIter++) {
                                    vBuff.push_back(*vIter);
                                }
#endif
                            }

                            nCurrWaitNo = 0;
                        }
                        else
                        {
                            nCurrWaitNo++;
                        }
                    }
                    
                    posBuf += framelen;

                    before_vadres = vadres;
                }

                } // only_record

				if (!item->flag) {	// 호가 종료되었음을 알리는 flag, 채널 갯수와 flag(0)이 들어온 갯수를 비교해야한다.
					//printf("\t[DEBUG] VRClient::thrdRxProcess(%s) - final item delivered.\n", client->m_sCallId.c_str());
                    client->m_Logger->debug("VRClient::thrdRxProcess(%s, %d) - final item delivered.", client->m_sCallId.c_str(), item->spkNo);

                    if ( !bOnlyRecord ) {

                    // send buff to gearman
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "LAST");
                    if (vBuff.size() > 0) {
                        for(size_t i=0; i<strlen(buf); i++) {
                            vBuff[i] = buf[i];
                        }
                    }
                    else {
                        for(size_t i=0; i<strlen(buf); i++) {
                            vBuff.push_back(buf[i]);
                        }
                    }
                    #ifdef EN_SAVE_PCM
                    if (bUseSavePcm)
                    {
                        FILE *pPcm;

                        std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_r.pcm");
                        pPcm = fopen(tempPcmFile.c_str(), "wb");
                        if (pPcm)
                        {
                            fwrite((const void*)&vBuff[0], sizeof(char), vBuff.size(), pPcm);
                            fclose(pPcm);
                        }
                    }
                    #endif
                    value= gearman_client_do(gearClient, fname/*"vr_realtime"*/, NULL, 
                                                    (const void*)&vBuff[0], vBuff.size(),
                                                    &result_size, &rc);
                    if (gearman_success(rc))
                    {
                        std::string svalue = (const char*)value;
                        svr_nm = svalue.substr(0, svalue.find("\n"));

                        free(value);

                        svalue.erase(0, svalue.find("\n")+1);
                        // Make use of value
                        if (svr_nm.size() && svalue.size()) {
                            std::string modValue = boost::replace_all_copy(svalue, "\n", " ");
                            // std::cout << "DEBUG : value(" << (char *)value << ") : size(" << result_size << ")" << std::endl;
                            //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);
                            diaNumber = client->thrdInfo.getDiaNumber();//client->getDiaNumber();
#ifdef USE_REDIS_POOL
                            if ( useRedis ) {
                                int64_t zCount=0;
                                std::string sJsonValue;
                                size_t in_size, out_size;
                                // iconv_t it;
                                char *utf_buf = NULL;
                                char *input_buf_ptr = NULL;
                                char *output_buf_ptr = NULL;

                                in_size = modValue.size();
                                out_size = in_size * 2 + 1;
                                utf_buf = (char *)malloc(out_size);

                                if (utf_buf) {
                                    memset(utf_buf, 0, out_size);

                                    input_buf_ptr = (char *)modValue.c_str();
                                    output_buf_ptr = utf_buf;

                                    // it = iconv_open("UTF-8", "EUC-KR");

                                    iconv(it, &input_buf_ptr, &in_size, &output_buf_ptr, &out_size);
                                    
                                    // iconv_close(it);

                                    {
                                        rapidjson::Document d;
                                        rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

                                        d.SetObject();
                                        d.AddMember("IDX", diaNumber, alloc);
                                        d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                        d.AddMember("SPK", rapidjson::Value("R", alloc).Move(), alloc);
                                        d.AddMember("POS_START", client->rx_sframe/10, alloc);
                                        d.AddMember("POS_END", client->rx_eframe/10, alloc);
                                        d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                        rapidjson::StringBuffer strbuf;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                        d.Accept(writer);

                                        sJsonValue = strbuf.GetString();
                                    }

                                    vVal.push_back(toString(diaNumber));
                                    vVal.push_back(sJsonValue);

                                    
                                    // vVal.push_back(toString(diaNumber));
                                    // vVal.push_back(modValue);

                                    if ( !xRedis.zadd(dbi, redisKey/*client->getCallId()*/, vVal, zCount) ) {
                                        client->m_Logger->error("VRClient::thrdRxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str()/*client->m_sCallId.c_str()*/, dbi.GetErrInfo(), zCount);
                                    }
                                    vVal.clear();

                                    free(utf_buf);
                                }
                            }
#endif

#ifdef DISABLE_ON_REALTIME
                            if (client->m_s2d) {
                                client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, client->rx_sframe/10, client->rx_eframe/10, modValue);
                            }
#endif // DISABLE_ON_REALTIME

                            //STTDeliver::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                            // to STTDeliver(file)
                            if (client->m_deliver) {
                                client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue, item->spkNo, client->rx_sframe/10, client->rx_eframe/10, client->m_sCounselCode);
                            }
                            
                        }
                    }
                    else if (gearman_failed(rc)){
                        client->m_Logger->error("VRClient::thrdRxProcess(%s) - failed gearman_client_do(). [%d : %d], timeout(%d)", client->m_sCallId.c_str(), client->rx_sframe, client->rx_eframe, client->m_nGearTimeout);
                    }

                    } // only_record

                    // and clear buff, set msg header
                    vBuff.clear();

                    if ( item->voiceData != NULL ) delete[] item->voiceData;
                    delete item;

                    if (client->m_is_save_pcm) {
                        // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_r.wav");
                        // std::ofstream pcmFile;

                        wHdr.Riff.ChunkSize = totalVoiceDataLen + sizeof(WAVE_HEADER) - 8;
                        wHdr.Data.ChunkSize = totalVoiceDataLen;

                        pcmFile.open(filename, ios::in | ios::out | ios::binary);
                        if (pcmFile.is_open()) {
                            pcmFile.seekp(0);
                            pcmFile.write((const char*)&wHdr, sizeof(WAVE_HEADER));
                            pcmFile.close();
                        }
                    }

                    client->thrdInfo.setRxState(0);//client->m_RxState = 0;
                    client->syncBreak = 1;
                    client->m_Logger->debug("VRClient::thrdRxProcess(%s) - FINISH CALL.(%d)", client->m_sCallId.c_str(), client->thrdInfo.getRxState());
                    break;
				}

				delete[] item->voiceData;
				delete item;
				// 예외 발생 시 처리 내용 : VDCManager의 removeVDC를 호출할 수 있어야 한다. - 이 후 VRClient는 item->flag(0)에 대해서만 처리한다.
			}
            //client->m_Logger->debug("VRClient::thrdMain(%s) - WHILE... [%d : %d], timeout(%d)", client->m_sCallId.c_str(), client->rx_sframe[item->spkNo -1], client->rx_eframe[item->spkNo -1], client->m_nGearTimeout);
			std::this_thread::sleep_for(std::chrono::microseconds(10));//milliseconds(1));
		}
        
        client->thrdInfo.setServerName(svr_nm); // client->setServerName(svr_nm);
        client->thrdInfo.setTotalVoiceDataLen(totalVoiceDataLen);// client->setTotalVoiceDataLen(totalVoiceDataLen);
        client->m_Logger->debug("VRClient::thrdRxProcess(%s) - ServerName(%s), TotalVoiceDataLen(%d)", client->m_sCallId.c_str(), client->thrdInfo.getServerName().c_str(), client->thrdInfo.getTotalVoiceDataLen());

        fvad_free(vad);

        std::vector<uint8_t>().swap(vBuff);

#if 0 // for DEBUG
		if (client->m_is_save_pcm && pcmFile.is_open()) pcmFile.close();
#endif
	}

    if ( !bOnlyRecord ) gearman_client_free(gearClient);

#ifdef USE_REDIS_POOL
    if ( useRedis ) 
        iconv_close(it);
#endif

    // search->setRxState(0);// client->m_RxState = 0;
    client->m_Logger->debug("VRClient::thrdRxProcess(%s) - RxState(%d)", client->m_sCallId.c_str(), client->thrdInfo.getRxState());
    // std::this_thread::sleep_for(std::chrono::milliseconds(1));
	// client->m_thrdRx.detach();
	// delete client;
}

void VRClient::thrdTxProcess(VRClient* client) {

	QueItem* item;
    gearman_client_st *gearClient;
    gearman_return_t ret;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;
    PosPair stPos;
    WAVE_HEADER wHdr;
    
    char buf[BUFLEN];
    uint16_t nHeadLen=0;
    
    uint8_t *vpBuf = NULL;
    size_t posBuf = 0;
    std::vector<uint8_t> vBuff;
#ifdef DIAL_SYNC_N_BUFF_CTRL
    std::vector<uint8_t> vTempBuff;
    std::vector<uint8_t>::iterator vIter;
#endif
    uint64_t totalVoiceDataLen;
    size_t framelen;
    std::string svr_nm;
    char fname[64];

    std::string fhCallId;
    char timebuff [32];
    char datebuff[32];
    struct tm * timeinfo = localtime(&client->m_tStart);
    strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",timeinfo);
    strftime (datebuff,sizeof(datebuff),"%Y%m%d",timeinfo);
    std::string fullpath = client->m_pcm_path + "/" + datebuff + "/" + client->m_sCounselCode + "/";
    std::string filename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + std::string("_l.wav");
    std::ofstream pcmFile;
    bool bOnlyRecord = !config->getConfig("stas.only_record", "false").compare("true");
    int nMinVBuffSize = config->getConfig("stas.min_buff_size", 10000);
    int nMaxWaitNo = config->getConfig("stas.max_wait_no", 7);
    int nCurrWaitNo = 0;
#ifdef EN_SAVE_PCM
    bool bUseSavePcm = !config->getConfig("stas.use_save_pcm", "false").compare("true");
    std::string pcmFilename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
#endif

    // auto search = client->ThreadInfoTable[client->m_sCallId];

    vBuff.reserve(MM_SIZE);

    MakeDirectory(fullpath.c_str());
    
    framelen = client->m_framelen * 2;

    sprintf(fname, "%s", client->m_sFname.c_str());

    fhCallId = std::string(timebuff) + "_" + client->m_sCallId;

#ifdef USE_REDIS_POOL
    bool useRedis = (!config->getConfig("redis.use", "false").compare("true") & !config->getConfig("redis.send_rt_stt", "false").compare("true"));
    iconv_t it;
    VALUES vVal;
    std::string sPubCannel = config->getConfig("redis.pubchannel", "RT-STT");
    xRedisClient &xRedis = client->getXRdedisClient();
    RedisDBIdx dbi(&xRedis);
    std::string redisKey = "G_RTSTT:";

    if ( useRedis ) {
        dbi.CreateDBIndex(client->getCallId().c_str(), APHash, CACHE_TYPE_1);
        it = iconv_open("UTF-8", "EUC-KR");
    }

    redisKey.append(client->getCallId());
#endif

    memcpy(wHdr.Riff.ChunkID, "RIFF", 4);
    wHdr.Riff.ChunkSize = 0;
    memcpy(wHdr.Riff.Format, "WAVE", 4);

    memcpy(wHdr.Fmt.ChunkID, "fmt ", 4);
    wHdr.Fmt.ChunkSize = 16;
    wHdr.Fmt.AudioFormat = 1;
    wHdr.Fmt.NumChannels = 1;
    wHdr.Fmt.SampleRate = 8000;
    wHdr.Fmt.AvgByteRate = 8000 * 1 * 16 / 8 ;
    wHdr.Fmt.BlockAlign = 1 * 16 / 8;
    wHdr.Fmt.BitPerSample = 16;

    memcpy(wHdr.Data.ChunkID, "data", 4);
    wHdr.Data.ChunkSize = 0;

    if (client->m_is_save_pcm) {
        pcmFile.open(filename, ios::out | ios::trunc | ios::binary);
        if (pcmFile.is_open()) {
            pcmFile.write((const char*)&wHdr, sizeof(WAVE_HEADER));
            pcmFile.close();
        }
    }
    
    stPos.bpos = 0;
    stPos.epos = 0;
    
    if ( !bOnlyRecord ) {
    
    gearClient = gearman_client_create(NULL);
    if (!gearClient) {
        //printf("\t[DEBUG] VRClient::thrdTxProcess() - ERROR (Failed gearman_client_create - %s)\n", client->m_sCallId.c_str());
        client->m_Logger->error("VRClient::thrdTxProcess() - ERROR (Failed gearman_client_create - %s)", client->m_sCallId.c_str());

        // WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        client->thrdInfo.setTxState(0);// client->m_TxState = 0;
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // client->m_thrdTx.detach();
        // delete client;
        return;
    }
    
    ret= gearman_client_add_server(gearClient, client->m_sGearHost.c_str(), client->m_nGearPort);
    if (gearman_failed(ret))
    {
        //printf("\t[DEBUG] VRClient::thrdTxProcess() - ERROR (Failed gearman_client_add_server - %s)\n", client->m_sCallId.c_str());
        client->m_Logger->error("VRClient::thrdTxProcess() - ERROR (Failed gearman_client_add_server - %s)", client->m_sCallId.c_str());

        // WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        client->thrdInfo.setTxState(0);//// client->m_TxState = 0;
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // client->m_thrdTx.detach();
        // delete client;
        return;
    }

    } // only_record
    

	// m_cJobType에 따라 작업 형태를 달리해야 한다. 
	if (client->m_cJobType == 'R') {
        uint32_t diaNumber=1;   // DB 실시간 STT 테이블에 저장될 호(Call)단위 Index 값
        Fvad *vad = NULL;
        int vadres, before_vadres;
        int aDianum;

        vad = fvad_new();
        if (!vad) {//} || (fvad_set_sample_rate(vad, in_info.samplerate) < 0)) {
            client->m_Logger->error("VRClient::thrdTxProcess() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
            if ( !bOnlyRecord ) gearman_client_free(gearClient);
            // WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);
            client->thrdInfo.setTxState(0);// client->m_TxState = 0;
            // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // client->m_thrdTx.detach();
            // delete client;
            return;
        }
        fvad_set_sample_rate(vad, 8000);
        fvad_set_mode(vad, client->m_mode);

		// 실시간의 경우 통화가 종료되기 전까지 Queue에서 입력 데이터를 받아 처리
		// FILE인 경우 기존과 동일하게 filename을 전달하는 방법 이용
        if (client->m_nGearTimeout) {
            if ( !bOnlyRecord ) gearman_client_set_timeout(gearClient, client->m_nGearTimeout);
        }
        
#if 0 // for DEBUG
		std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_") + std::to_string(client->m_nNumofChannel) + std::string(".pcm");
		std::ofstream pcmFile;
        if (client->m_is_save_pcm)
            pcmFile.open(filename, ios::out | ios::app | ios::binary);
#endif

        // write wav heaer to file(mmap);
        vBuff.clear();
        client->tx_sframe = 0;
        // client->tx_eframe = 0;
        client->tx_hold = 0;
        aDianum = 0;
        totalVoiceDataLen = 0;
        svr_nm = "DEFAULT";

        vadres = before_vadres = 0;
		while (client->thrdInfo.getTxState())//(client->m_nLiveFlag)
		{
			while (!client->m_qTXQue.empty()) {
				// g = new std::lock_guard<std::mutex>(client->m_mxQue);
				item = client->m_qTXQue.front();
				client->m_qTXQue.pop();
				// delete g;

                totalVoiceDataLen += item->lenVoiceData;

                stPos.epos += item->lenVoiceData;
				// queue에서 가져온 item을 STT 하는 로직을 아래에 코딩한다.
				// call-id + item->spkNo => call-id for rt-stt
                memset(buf, 0, sizeof(buf));
                if (!item->flag) {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "LAST");
                }
                else if (item->flag == 2) {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "FIRS");
                }
                else {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "NOLA");
                }
                nHeadLen = strlen(buf);

                if (vBuff.size()>0) {
                    for(int i=0; i<nHeadLen; i++) {
                        vBuff[i] = buf[i];
                    }
                }
                else {
                    for(int i=0; i<nHeadLen; i++) {
                        vBuff.push_back(buf[i]);
                    }
                }
                

                if (client->m_is_save_pcm) {
                    // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_l.wav");
                    // std::ofstream pcmFile;

                    pcmFile.open(filename, ios::out | ios::app | ios::binary);
                    if (pcmFile.is_open()) {
                        pcmFile.write((const char*)item->voiceData, item->lenVoiceData);
                        pcmFile.close();
                    }
                }

                if ( !bOnlyRecord ) {
                
                // check vad!, by loop()
                // if finish check vad and vBuff is no empty, send buff to VR by gearman
                // vadres == 1 vBuff[item->spkNo-1].push_back();
                // vadres == 0 and vBuff[item->spkNo-1].size() > 0 then send buff to gearman
                posBuf = 0;
                while ((item->lenVoiceData >= framelen) && ((item->lenVoiceData - posBuf) >= framelen)) {
                    vpBuf = (uint8_t *)(item->voiceData+posBuf);
#ifdef CHANNEL_SYNC
                    // for channel sync
                    while (client->rx_eframe < client->tx_eframe) {
                        if (
                            client->syncBreak ||
#ifdef DIAL_SYNC 
                            client->rx_hold || 
#endif
                            !item->flag) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
#endif
                    client->tx_eframe += (client->m_framelen/8);

                    // Convert the read samples to int16
                    vadres = fvad_process(vad, (const int16_t *)vpBuf, client->m_framelen);

                    if (vadres < 0) {
                        //client->m_Logger->error("VRClient::thrdMain(%d, %d, %s)(%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", nHeadLen, item->spkNo, buf, client->m_sCallId.c_str(), vBuff[item->spkNo-1].size(), client->tx_sframe[item->spkNo-1], client->tx_eframe[item->spkNo-1]);
                        continue;
                    }

                    if (vadres > 0) {
                        for(size_t i=0; i<framelen; i++) {
                            vBuff.push_back(vpBuf[i]);
                            
                        }
                    }
                    
                    if (!vadres && (vBuff.size()<=nHeadLen)) {
                        // start ms
                        client->tx_sframe = client->tx_eframe - (client->m_framelen/8);//20;
                    }
#ifdef DEBUGING
                    if (vadres && !before_vadres) {
                        // 음성 시작 점 - Voice Active Detection Poing
                        std::string filename = client->m_sCallId + std::string("_vadpoint_l.txt");
                        std::ofstream pcmFile;

                        pcmFile.open(filename, ios::out | ios::app);
                        if (pcmFile.is_open()) {
                            pcmFile << client->tx_sframe << " ";
                            pcmFile.close();
                        }
                    }

                    if (!vadres && before_vadres) {
                        // 음성 시작 점 - Voice Active Detection Poing
                        std::string filename = client->m_sCallId + std::string("_vadpoint_l.txt");
                        std::ofstream pcmFile;

                        pcmFile.open(filename, ios::out | ios::app);
                        if (pcmFile.is_open()) {
                            pcmFile << client->tx_eframe << std::endl;
                            pcmFile.close();
                        }
                    }
#endif
                    if (
                        client->rx_hold ||
#ifdef DIAL_SYNC_N_BUFF_CTRL
                        (client->rx_hold && (client->tx_sframe < client->rx_sframe)) || 
#endif
                        (!vadres && (vBuff.size()>nHeadLen))) {
#ifdef DIAL_SYNC_N_BUFF_CTRL
                        vTempBuff.clear();
                        if ((vBuff.size() > 15000) && client->rx_hold && (client->tx_sframe < client->rx_sframe)) {
                            size_t offset = (((client->rx_sframe - client->tx_sframe) * 16) + nHeadLen);
                            vTempBuff.assign(vBuff.begin() + offset, vBuff.end());
                            vBuff.erase(vBuff.begin() + offset, vBuff.end());
                        }
#endif
                        if ( (nCurrWaitNo > nMaxWaitNo) || (vBuff.size() > nMinVBuffSize) ) {   // 8000 bytes, 0.5 이하의 음성데이터는 처리하지 않음
#if 0 // VR로 데이터처리 요청 시 처리할 데이터의 sframe, eframe, buff.size 출력
                            if (1) {
                                // 음성 시작 점 - Voice Active Detection Poing
                                std::string filename = client->m_sCallId + std::string("_vadpoint_l.txt");
                                std::ofstream pcmFile;

                                pcmFile.open(filename, ios::out | ios::app);
                                if (pcmFile.is_open()) {
                                    pcmFile << client->tx_sframe << " " << client->tx_eframe << " " << vBuff.size()-nHeadLen << std::endl;
                                    pcmFile.close();
                                }
                            }
#endif

#ifdef DIAL_SYNC
                            if (!client->rx_hold) {
                                client->tx_hold = 1;
                                while (client->tx_hold) {
                                    if (!item->flag) break;
                                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                            }
#endif
                            // send buff to gearman
                            if (aDianum == 0) {
                                sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "FIRS");
                            }
                            else {
                                sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "NOLA");
                            }
                            for(size_t i=0; i<strlen(buf); i++) {
                                vBuff[i] = buf[i];
                            }
                            //client->m_Logger->debug("VRClient::thrdMain(%d, %d, %s)(%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", nHeadLen, item->spkNo, buf, client->m_sCallId.c_str(), vBuff[item->spkNo-1].size(), client->tx_sframe[item->spkNo-1], client->tx_eframe[item->spkNo-1]);
                            #ifdef EN_SAVE_PCM
                            if (bUseSavePcm)
                            {
                                FILE *pPcm;

                                std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_l.pcm");
                                pPcm = fopen(tempPcmFile.c_str(), "wb");
                                if (pPcm)
                                {
                                    fwrite((const void*)&vBuff[0], sizeof(char), vBuff.size(), pPcm);
                                    fclose(pPcm);
                                }
                            }
                            #endif
                            value= gearman_client_do(gearClient, fname/*"vr_realtime"*/, NULL, 
                                                            (const void*)&vBuff[0], vBuff.size(),
                                                            &result_size, &rc);
                                                            
                            aDianum++;
                            
                            if (gearman_success(rc))
                            {
                                // Make use of value
                                if (value) {
                                    std::string modValue = boost::replace_all_copy(std::string((const char*)value), "\n", " ");
                                    // std::cout << "DEBUG : value(" << (char *)value << ") : size(" << result_size << ")" << std::endl;
                                    //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);
                                    diaNumber = client->thrdInfo.getDiaNumber();//client->getDiaNumber();
#ifdef USE_REDIS_POOL
                                    if ( useRedis ) {
                                        int64_t zCount=0;
                                        std::string sJsonValue;

                                        size_t in_size, out_size;
                                        // iconv_t it;
                                        char *utf_buf = NULL;
                                        char *input_buf_ptr = NULL;
                                        char *output_buf_ptr = NULL;

                                        in_size = modValue.size();
                                        out_size = in_size * 2 + 1;
                                        utf_buf = (char *)malloc(out_size);

                                        if (utf_buf) {
                                            memset(utf_buf, 0, out_size);

                                            input_buf_ptr = (char *)modValue.c_str();
                                            output_buf_ptr = utf_buf;

                                            iconv(it, &input_buf_ptr, &in_size, &output_buf_ptr, &out_size);

                                            {
                                                rapidjson::Document d;
                                                rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

                                                d.SetObject();
                                                d.AddMember("IDX", diaNumber, alloc);
                                                d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                                d.AddMember("SPK", rapidjson::Value("L", alloc).Move(), alloc);
                                                d.AddMember("POS_START", client->tx_sframe/10, alloc);
                                                d.AddMember("POS_END", client->tx_eframe/10, alloc);
                                                d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                                rapidjson::StringBuffer strbuf;
                                                rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                                d.Accept(writer);

                                                sJsonValue = strbuf.GetString();
                                            }

                                            vVal.push_back(toString(diaNumber));
                                            vVal.push_back(sJsonValue);

                                            if ( !xRedis.zadd(dbi, redisKey/*client->getCallId()*/, vVal, zCount) ) {
                                                client->m_Logger->error("VRClient::thrdTxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str()/*client->m_sCallId.c_str()*/, dbi.GetErrInfo(), zCount);
                                            }
                                            vVal.clear();

                                            free(utf_buf);
                                        }
                                    }
#endif

#ifdef DISABLE_ON_REALTIME
                                    // to DB
                                    if (client->m_s2d) {
                                        client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, client->tx_sframe/10, client->tx_eframe/10, modValue);
                                    }
#endif // DISABLE_ON_REALTIME

                                    //STTDeliver::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                                    // to STTDeliver(file)
                                    if (client->m_deliver) {
                                        client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue, item->spkNo, client->tx_sframe/10, client->tx_eframe/10, client->m_sCounselCode);
                                    }

                                    free(value);
                                    
                                }
                            }
                            else if (gearman_failed(rc)){
                                client->m_Logger->error("VRClient::thrdTxProcess(%s) - failed gearman_client_do(). [%lu : %lu], timeout(%d)", client->m_sCallId.c_str(), client->tx_sframe, client->tx_eframe, client->m_nGearTimeout);
                            }

                            // and clear buff, set msg header
                            vBuff.clear();

                            for(size_t i=0; i<nHeadLen; i++) {
                                //vBuff[item->spkNo-1][i] = buf[i];
                                vBuff.push_back(buf[i]);

                            }
                            client->tx_sframe = client->tx_eframe;

                            if (client->rx_hold) {
                                client->rx_hold = 0;
#ifdef DIAL_SYNC_N_BUFF_CTRL
                                for(vIter=vTempBuff.begin(); vIter!=vTempBuff.end(); vIter++) {
                                    vBuff.push_back(*vIter);
                                }
#endif
                            }

                            nCurrWaitNo = 0;

                        }
                        else
                        {
                            nCurrWaitNo++;
                        }
                    }
                    
                    posBuf += framelen;

                    before_vadres = vadres;
                }

                } // only_record

				if (!item->flag) {	// 호가 종료되었음을 알리는 flag, 채널 갯수와 flag(0)이 들어온 갯수를 비교해야한다.
					//printf("\t[DEBUG] VRClient::thrdTxProcess(%s) - final item delivered.\n", client->m_sCallId.c_str());
                    client->m_Logger->debug("VRClient::thrdTxProcess(%s, %d) - final item delivered.", client->m_sCallId.c_str(), item->spkNo);

                    if ( !bOnlyRecord ) {

                    // send buff to gearman
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "LAST");
                    if (vBuff.size() > 0) {
                        for(size_t i=0; i<strlen(buf); i++) {
                            vBuff[i] = buf[i];
                        }
                    }
                    else {
                        for(size_t i=0; i<strlen(buf); i++) {
                            vBuff.push_back(buf[i]);
                        }
                    }
                    #ifdef EN_SAVE_PCM
                    if (bUseSavePcm)
                    {
                        FILE *pPcm;

                        std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_l.pcm");
                        pPcm = fopen(tempPcmFile.c_str(), "wb");
                        if (pPcm)
                        {
                            fwrite((const void*)&vBuff[0], sizeof(char), vBuff.size(), pPcm);
                            fclose(pPcm);
                        }
                    }
                    #endif
                    value= gearman_client_do(gearClient, fname/*"vr_realtime"*/, NULL, 
                                                    (const void*)&vBuff[0], vBuff.size(),
                                                    &result_size, &rc);
                    if (gearman_success(rc))
                    {
                        std::string svalue = (const char*)value;
                        svr_nm = svalue.substr(0, svalue.find("\n"));

                        free(value);

                        svalue.erase(0, svalue.find("\n")+1);
                        // Make use of value
                        if (svr_nm.size() && svalue.size()) {
                            std::string modValue = boost::replace_all_copy(svalue, "\n", " ");
                            // std::cout << "DEBUG : value(" << (char *)value << ") : size(" << result_size << ")" << std::endl;
                            //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);
                            diaNumber = client->thrdInfo.getDiaNumber();//client->getDiaNumber();
#ifdef USE_REDIS_POOL
                            if ( useRedis ) {
                                int64_t zCount=0;
                                std::string sJsonValue;
                                size_t in_size, out_size;
                                // iconv_t it;
                                char *utf_buf = NULL;
                                char *input_buf_ptr = NULL;
                                char *output_buf_ptr = NULL;

                                in_size = modValue.size();
                                out_size = in_size * 2 + 1;
                                utf_buf = (char *)malloc(out_size);

                                if (utf_buf) {
                                    memset(utf_buf, 0, out_size);

                                    input_buf_ptr = (char *)modValue.c_str();
                                    output_buf_ptr = utf_buf;

                                    iconv(it, &input_buf_ptr, &in_size, &output_buf_ptr, &out_size);

                                    {
                                        rapidjson::Document d;
                                        rapidjson::Document::AllocatorType& alloc = d.GetAllocator();

                                        d.SetObject();
                                        d.AddMember("IDX", diaNumber, alloc);
                                        d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                        d.AddMember("SPK", rapidjson::Value("L", alloc).Move(), alloc);
                                        d.AddMember("POS_START", client->tx_sframe/10, alloc);
                                        d.AddMember("POS_END", client->tx_eframe/10, alloc);
                                        d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                        rapidjson::StringBuffer strbuf;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                        d.Accept(writer);

                                        sJsonValue = strbuf.GetString();
                                    }

                                    vVal.push_back(toString(diaNumber));
                                    vVal.push_back(sJsonValue);

                                    if ( !xRedis.zadd(dbi, redisKey/*client->getCallId()*/, vVal, zCount) ) {
                                        client->m_Logger->error("VRClient::thrdTxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str()/*client->m_sCallId.c_str()*/, dbi.GetErrInfo(), zCount);
                                    }
                                    vVal.clear();

                                    free(utf_buf);
                                }
                            }
#endif

#ifdef DISABLE_ON_REALTIME

                            if (client->m_s2d) {
                                client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, client->tx_sframe/10, client->tx_eframe/10, modValue);
                            }
#endif // DISABLE_ON_REALTIME

                            //STTDeliver::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                            // to STTDeliver(file)
                            if (client->m_deliver) {
                                client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue, item->spkNo, client->tx_sframe/10, client->tx_eframe/10, client->m_sCounselCode);
                            }
                            
                        }
                    }
                    else if (gearman_failed(rc)){
                        client->m_Logger->error("VRClient::thrdTxProcess(%s) - failed gearman_client_do(). [%d : %d], timeout(%d)", client->m_sCallId.c_str(), client->tx_sframe, client->tx_eframe, client->m_nGearTimeout);
                    }

                    } // only_record

                    // and clear buff, set msg header
                    vBuff.clear();

                    if ( item->voiceData != NULL ) delete[] item->voiceData;
                    delete item;

                    if (client->m_is_save_pcm) {
                        // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_l.wav");
                        // std::ofstream pcmFile;

                        wHdr.Riff.ChunkSize = totalVoiceDataLen + sizeof(WAVE_HEADER) - 8;
                        wHdr.Data.ChunkSize = totalVoiceDataLen;

                        pcmFile.open(filename, ios::in | ios::out | ios::binary);
                        if (pcmFile.is_open()) {
                            pcmFile.seekp(0);
                            pcmFile.write((const char*)&wHdr, sizeof(WAVE_HEADER));
                            pcmFile.close();
                        }
                    }
                    client->thrdInfo.setTxState(0);// client->m_TxState = 0;
                    client->syncBreak = 1;
                    client->m_Logger->debug("VRClient::thrdTxProcess(%s) - FINISH CALL.(%d)", client->m_sCallId.c_str(), client->thrdInfo.getTxState());
                    break;
				}

				delete[] item->voiceData;
				delete item;
				// 예외 발생 시 처리 내용 : VDCManager의 removeVDC를 호출할 수 있어야 한다. - 이 후 VRClient는 item->flag(0)에 대해서만 처리한다.
			}
            //client->m_Logger->debug("VRClient::thrdMain(%s) - WHILE... [%d : %d], timeout(%d)", client->m_sCallId.c_str(), client->tx_sframe[item->spkNo -1], client->tx_eframe[item->spkNo -1], client->m_nGearTimeout);
			std::this_thread::sleep_for(std::chrono::microseconds(10));//milliseconds(1));
		}
        
        // client->thrdInfo.setServerName(svr_nm);// client->setServerName(svr_nm);
        // client->thrdInfo.setTotalVoiceDataLen(totalVoiceDataLen);// client->setTotalVoiceDataLen(totalVoiceDataLen);
        client->m_Logger->debug("VRClient::thrdTxProcess(%s) - ServerName(%s), TotalVoiceDataLen(%d)", client->m_sCallId.c_str(), svr_nm.c_str(), totalVoiceDataLen);

        fvad_free(vad);

        std::vector<uint8_t>().swap(vBuff);

#if 0 // for DEBUG
		if (client->m_is_save_pcm && pcmFile.is_open()) pcmFile.close();
#endif
	}

    if ( !bOnlyRecord ) gearman_client_free(gearClient);

#ifdef USE_REDIS_POOL
    if ( useRedis )
        iconv_close(it);
#endif

    // search->setTxState(0);// client->m_TxState = 0;
    client->m_Logger->debug("VRClient::thrdTxProcess(%s) - TxState(%d)", client->m_sCallId.c_str(), client->thrdInfo.getTxState());
    // std::this_thread::sleep_for(std::chrono::milliseconds(1));
	// client->m_thrdTx.detach();
}

void VRClient::insertQueItem(QueItem* item)
{
	std::lock_guard<std::mutex> g(m_mxQue);
	// m_qRTQue.push(item);

    if (item->spkNo == 1)
        m_qRXQue.push(item);
    else if (item->spkNo == 2)
        m_qTXQue.push(item);
    else {
        delete[] item->voiceData;
        delete item;
    }
}

#ifdef USE_REDIS_POOL
xRedisClient& VRClient::getXRdedisClient()
{
    // return m_Mgr->getRedisClient();
    return RedisHandler::instance()->getRedisClient();
}
#endif





#endif

#endif // ENABLE_REALTIME
