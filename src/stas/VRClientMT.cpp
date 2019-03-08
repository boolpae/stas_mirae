#ifdef  ENABLE_REALTIME
#ifdef USE_REALTIME_MT

#include "VRClientMT.h"
#include "VRCManager.h"
#include "WorkTracer.h"
#include "FileHandler.h"

#include "DBHandler.h"

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


#ifdef EN_RINGBACK_LEN
VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT, uint32_t ringbacklen)
#else
VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT)
#endif
	: m_sGearHost(gearHost), m_nGearPort(gearPort), m_nGearTimeout(gearTimeout), m_sFname(fname), m_sCallId(callid), m_sCounselCode(counselcode), m_nLiveFlag(1), m_cJobType(jobType), m_nNumofChannel(noc), m_deliver(deliver), m_s2d(s2d), m_is_save_pcm(is_save_pcm), m_pcm_path(pcm_path), m_framelen(framelen*8), m_mode(mode)
{

	m_Mgr = mgr;

#ifdef EN_RINGBACK_LEN
    rx_sframe = rx_eframe=ringbacklen;
    tx_sframe = tx_eframe=ringbacklen;
#else
    rx_sframe=0;
    tx_sframe=0;
    rx_eframe=0;
    tx_eframe=0;
#endif
    syncBreak = 0;

    rx_hold = 0;
    tx_hold = 0;

    m_tStart = startT;
#ifdef EN_RINGBACK_LEN
    m_nRingbackLen = ringbacklen;
#endif

	sprintf(ServerName,"%s", "DEFAULT");
	TotalVoiceDataLen=0;
	DiaNumber=0;
	RxState=1;
	TxState=1;

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

	while (!m_qRXQue.empty()) {
		item = m_qRXQue.front();
		m_qRXQue.pop();

		delete item;
	}

	while (!m_qTXQue.empty()) {
		item = m_qTXQue.front();
		m_qTXQue.pop();

		delete item;
	}

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
#define MM_SIZE (1024 * 1024 * 5)

void VRClient::thrdMain(VRClient* client) {
    char datebuff[32];
    char timebuff [32];
    struct tm timeinfo;
    std::string sPubCannel = config->getConfig("redis.pubchannel", "RT-STT");
    bool useDelCallInfo = !config->getConfig("stas.use_del_callinfo", "false").compare("true");
    unsigned int nDelSecs = config->getConfig("stas.del_secs", 0);

#ifdef USE_REDIS_POOL
    int64_t zCount=0;
    std::string redisKey = "G_CS:";
    char redisValue[256];
    std::string strRedisValue;
    bool useRedis = !config->getConfig("redis.use", "false").compare("true");
    bool bSendDataRedis = !config->getConfig("redis.send_rt_stt", "false").compare("true");
    xRedisClient &xRedis = client->getXRdedisClient();
    RedisDBIdx dbi(&xRedis);
    VALUES vVal;
#endif
    bool bSaveJsonData = !config->getConfig("stas.save_json_data", "false").compare("true");

    localtime_r(&client->m_tStart, &timeinfo);

#ifdef USE_REDIS_POOL
    if ( useRedis ) 
    {
        dbi.CreateDBIndex(client->getCallId().c_str(), APHash, CACHE_TYPE_1);

        // 2019-01-10, 호 시작 시 상담원 상태 변경 전달 - 호 시작
        zCount=0;
        redisKey.append(client->getCounselCode());

        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",&timeinfo);
        sprintf(redisValue, "{\"REG_DTM\":\"%s\", \"STATE\":\"I\", \"CALL_ID\":\"%s\"}", timebuff, client->getCallId().c_str());
        strRedisValue = redisValue;
        
        vVal.push_back( strRedisValue );
        if ( bSendDataRedis )
        {
            xRedis.lpush( dbi, redisKey, vVal, zCount );
        }
        vVal.clear();

        redisKey = "G_RTSTT:";
        redisKey.append(client->getCallId());
    }
#endif

    auto t1 = std::chrono::high_resolution_clock::now();
      
    while (client->RxState || client->TxState)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    client->m_Logger->debug("VRClient::thrdMain(%s) - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->ServerName, client->TotalVoiceDataLen);

    client->m_Mgr->removeVRC(client->m_sCallId);

    client->m_Logger->debug("VRClient::thrdMain(%s)[2] - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->ServerName, client->TotalVoiceDataLen);

#ifdef USE_REDIS_POOL
    if ( useRedis ) {
        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = localtime(&t);

        if ( bSendDataRedis )
        {
            if ( !xRedis.publish(dbi, sPubCannel.c_str(), client->getCallId().c_str(), zCount) )
                client->m_Logger->error("VRClient::thrdMain(%s) - redis publish(). [%s], zCount(%d)", client->m_sCallId.c_str(), dbi.GetErrInfo(), zCount);
        }

        redisKey = "G_CS:";
        redisKey.append(client->getCounselCode());

        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",tmp);
        sprintf(redisValue, "{\"REG_DTM\":\"%s\", \"STATE\":\"E\", \"CALL_ID\":\"%s\"}", timebuff, client->getCallId().c_str());
        strRedisValue = redisValue;
        
        if ( bSendDataRedis )
        {
            xRedis.lset(dbi, redisKey, 0, strRedisValue);
        }

    }
#endif

    // for TEST
    if (bSaveJsonData && client->m_deliver) {
        std::string fhCallId;
        std::string emptyStr("");
        strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",&timeinfo);
        fhCallId = std::string(timebuff) + "_" + client->m_sCallId;
        client->m_deliver->insertJsonData(fhCallId, emptyStr, 2, 0, 0, client->m_sCounselCode);
    }

    client->m_Logger->debug("VRClient::thrdMain(%s)[3] - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->ServerName, client->TotalVoiceDataLen);

    if (client->m_s2d) {
        auto t2 = std::chrono::high_resolution_clock::now();
        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",&timeinfo);

        client->m_s2d->updateCallInfo(client->m_sCallId, true);
        if ( !useDelCallInfo || ( client->TotalVoiceDataLen/16000 > nDelSecs ) ) {
            client->m_s2d->updateTaskInfo(client->m_sCallId, std::string(timebuff), std::string("MN"), client->m_sCounselCode, 'Y', client->TotalVoiceDataLen, client->TotalVoiceDataLen/16000, std::chrono::duration_cast<std::chrono::seconds>(t2-t1).count(), 0, "STT_TBL_JOB_INFO", "", client->ServerName);
        }
    }
    client->m_Logger->debug("VRClient::thrdMain(%s)[4] - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->ServerName, client->TotalVoiceDataLen);

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

            strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",&timeinfo);
            strftime (datebuff,sizeof(datebuff),"%Y%m%d",&timeinfo);

            cmd = config->getConfig("stas.merge");
            cmd.push_back(' ');
            cmd.append(client->m_pcm_path.c_str());
            cmd.push_back('/');
            cmd.append(datebuff);
            cmd.push_back('/');
            cmd.append(client->m_sCounselCode.c_str());
            cmd.push_back(' ');
            cmd.append(client->m_sCounselCode.c_str());
            cmd.push_back('_');
            cmd.append(timebuff);
            cmd.push_back('_');
            cmd.append(client->m_sCallId.c_str());

            if (std::system(cmd.c_str())) {
                client->m_Logger->error("VRClient::thrdMain(%s) Fail to merge wavs: command(%s)", client->m_sCallId.c_str(), cmd.c_str());
            }
        }
    }
    client->m_Logger->debug("VRClient::thrdMain(%s) - FINISH CALL.", client->m_sCallId.c_str());

	WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

    // 3초 이하 호 정보 삭제 - client->TotalVoiceDataLen/16000 < 3 인경우 호 정보 삭제
    if ( useDelCallInfo && nDelSecs ) {
        if ( client->m_is_save_pcm && (client->TotalVoiceDataLen/16000 <= nDelSecs) )
        {
            char datebuff[32];

            strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",&timeinfo);
            strftime (datebuff,sizeof(datebuff),"%Y%m%d",&timeinfo);
            std::string fullpath = client->m_pcm_path + "/" + datebuff + "/" + client->m_sCounselCode + "/";
            for (int i=0; i<2; i++) {
                std::string spker = (i == 0)?std::string("_r.wav"):std::string("_l.wav");
                std::string filename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + spker;
                remove( filename.c_str() ) ;
            }
        }
        client->m_Logger->debug("VRClient::thrdMain(%s)[5] - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->ServerName, client->TotalVoiceDataLen);

        if ( useRedis && (client->TotalVoiceDataLen/16000 <= nDelSecs) ) {
            redisKey = "G_CS:";
            redisKey.append(client->m_sCounselCode);

            if ( bSendDataRedis )
            {
                xRedis.lpop(dbi, redisKey, strRedisValue);
            }

            redisKey = "G_RTSTT:";
            redisKey.append(client->m_sCallId);

            if ( bSendDataRedis )
            {
                xRedis.del(dbi, redisKey);
            }

        }
        if ( client->m_s2d && (client->TotalVoiceDataLen/16000 <= nDelSecs) ) {
            client->m_s2d->deleteJobInfo(client->m_sCallId);
        }
    }
    client->m_Logger->debug("VRClient::thrdMain(%s)[6] - ServerName(%s), TotalVoiceLen(%d)", client->m_sCallId.c_str(), client->ServerName, client->TotalVoiceDataLen);

	delete client;
}

void VRClient::thrdRxProcess(VRClient* client) {

	QueItem* item;
    gearman_client_st *gearClient = nullptr;
    gearman_return_t ret;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;
    PosPair stPos;
    WAVE_HEADER wHdr;
    
    char buf[BUFLEN];
    uint16_t nHeadLen=0;
    int chkRealSize=0;
    
    uint8_t *vpBuf = NULL;
    size_t posBuf = 0;
    std::vector<uint8_t> vBuff;
    uint64_t totalVoiceDataLen;
    size_t framelen;
    std::string svr_nm;
    char fname[64];

    #ifdef USE_FIND_KEYWORD
    std::list< std::string > keywordList;
    std::list< std::string >::iterator klIter;
    #endif


    std::string fhCallId;
    char timebuff [32];
    char datebuff[32];
    struct tm timeinfo;
    std::string fullpath;
    std::string filename;
    std::ofstream pcmFile;
    bool bOnlyRecord = !config->getConfig("stas.only_record", "false").compare("true");
    int nMinVBuffSize = config->getConfig("stas.min_buff_size", 10000);

#ifdef EN_SAVE_PCM
    bool bOnlySil;
    bool bUseSavePcm;
    std::string pcmFilename;
#endif

    bool bUseSkipHanum = !config->getConfig("stas.use_skip_hanum", "false").compare("true");
    unsigned int nSkipHanumSize = config->getConfig("stas.skip_hanum_buff_size", 16000);

    bool bUseFindKeyword = !config->getConfig("stas.use_find_keyword", "fasle").compare("true");
    bool bUseRemSpaceInNumwords = !config->getConfig("stas.use_rem_space_numwords", "false").compare("true");

    bool bSaveJsonData = !config->getConfig("stas.save_json_data", "false").compare("true");

    localtime_r(&client->m_tStart, &timeinfo);
    strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",&timeinfo);
    strftime (datebuff,sizeof(datebuff),"%Y%m%d",&timeinfo);

    fullpath = client->m_pcm_path + "/" + datebuff + "/" + client->m_sCounselCode + "/";
    filename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + std::string("_r.wav");
#ifdef EN_SAVE_PCM
    bOnlySil = !config->getConfig("stas.only_silence", "false").compare("true");
    bUseSavePcm = !config->getConfig("stas.use_save_pcm", "false").compare("true");

    if ( (bOnlySil || bUseSavePcm) && config->isSet("stas.pcm_path") && (config->getConfig("stas.pcm_path", "").size() > 0) )
    {
        pcmFilename = config->getConfig("stas.pcm_path", client->m_pcm_path.c_str()) + "/" + datebuff + "/" + client->m_sCounselCode + "/";
        MakeDirectory(pcmFilename.c_str());
        pcmFilename = pcmFilename + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
    }
    else pcmFilename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
#endif

    vBuff.reserve(MM_SIZE);

    MakeDirectory(fullpath.c_str());
    
    framelen = client->m_framelen * 2;

    sprintf(fname, "%s", client->m_sFname.c_str());

    fhCallId = std::string(timebuff) + "_" + client->m_sCallId;

#ifdef USE_REDIS_POOL
    bool useRedis = !config->getConfig("redis.use", "false").compare("true");
    bool bSendDataRedis = !config->getConfig("redis.send_rt_stt", "false").compare("true");
    iconv_t it = NULL;
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

#ifdef USE_FIND_KEYWORD
    if ( bUseFindKeyword ) 
    {
        keywordList = DBHandler::getKeywords();
    }
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
        client->m_Logger->error("VRClient::thrdRxProcess() - ERROR (Failed gearman_client_create - %s)", client->m_sCallId.c_str());

        client->RxState = 0;
        return;
    }
    
    ret= gearman_client_add_server(gearClient, client->m_sGearHost.c_str(), client->m_nGearPort);
    if (gearman_failed(ret))
    {
        client->m_Logger->error("VRClient::thrdRxProcess() - ERROR (Failed gearman_client_add_server - %s)", client->m_sCallId.c_str());

        client->RxState =0;
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
        if (!vad) {
            client->m_Logger->error("VRClient::thrdRxProcess() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
            if ( !bOnlyRecord ) gearman_client_free(gearClient);
            client->RxState = 0;
            return;
        }
        fvad_set_sample_rate(vad, 8000);
        fvad_set_mode(vad, client->m_mode);

		// 실시간의 경우 통화가 종료되기 전까지 Queue에서 입력 데이터를 받아 처리
		// FILE인 경우 기존과 동일하게 filename을 전달하는 방법 이용
        if (client->m_nGearTimeout) {
            if ( !bOnlyRecord ) gearman_client_set_timeout(gearClient, client->m_nGearTimeout);
        }
        

        // write wav heaer to file(mmap);
        vBuff.clear();
        client->rx_hold = 0;
        aDianum = 0;
        totalVoiceDataLen = 0;
        svr_nm = "DEFAULT";

        vadres = before_vadres = 0;
		while (client->RxState)
		{
			while (!client->m_qRXQue.empty()) {
				item = client->m_qRXQue.front();
				client->m_qRXQue.pop();

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
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
#endif
                    client->rx_eframe += (client->m_framelen/8);

                    // Convert the read samples to int16
                    vadres = fvad_process(vad, (const int16_t *)vpBuf, client->m_framelen);

                    if (vadres < 0) {
                        continue;
                    }

                    if (vadres > 0) {
                        // 직전 버퍼 값을 사용... 인식률 향상 확인용
                        if (posBuf && (vBuff.size() == nHeadLen))
                        {
                            vpBuf = (uint8_t *)(item->voiceData+posBuf-framelen);
                            for(size_t i=0; i<framelen; i++) {
                                vBuff.push_back(vpBuf[i]);
                            }
                            vpBuf = (uint8_t *)(item->voiceData+posBuf);
                        }
                        for(size_t i=0; i<framelen; i++) {
                            vBuff.push_back(vpBuf[i]);
                            
                        }
                    }
                    
                    if (!vadres && (vBuff.size()<=nHeadLen)) {
                        // start ms
                        client->rx_sframe = client->rx_eframe - (client->m_framelen/8);
                    }

                    if (
                        client->tx_hold ||
                        (!vadres && (vBuff.size()>nHeadLen))) {
                        chkRealSize = checkRealSize(vBuff, nHeadLen, framelen, client->m_framelen);
                        
                        if ( chkRealSize > nMinVBuffSize ) {   // 8000 bytes, 0.5 이하의 음성데이터는 처리하지 않음

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

                            #ifdef EN_SAVE_PCM
                            if (!bOnlySil && bUseSavePcm)
                            {
                                FILE *pPcm;

                                std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_r.pcm");
                                pPcm = fopen(tempPcmFile.c_str(), "wb");
                                if (pPcm)
                                {
                                    fwrite((const void*)&vBuff[nHeadLen], sizeof(char), vBuff.size()-nHeadLen, pPcm);
                                    fclose(pPcm);
                                }
                            }
                            #endif
                            value= gearman_client_do(gearClient, fname, NULL, 
                                                            (const void*)&vBuff[0], vBuff.size(),
                                                            &result_size, &rc);
                                                            
                            aDianum++;
                            
                            if (gearman_success(rc))
                            {
                                #ifdef EN_SAVE_PCM
                                if (bOnlySil && (result_size < 4))
                                {
                                    FILE *pPcm;

                                    std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_r.pcm");
                                    pPcm = fopen(tempPcmFile.c_str(), "wb");
                                    if (pPcm)
                                    {
                                        fwrite((const void*)&vBuff[nHeadLen], sizeof(char), vBuff.size()-nHeadLen, pPcm);
                                        fclose(pPcm);
                                    }
                                }
                                #endif

                                // n초 이상 음성데이터에 대해 네, 예 등과 같이 한음절 응답이 경우 무시
                                if ( bUseSkipHanum && value && (vBuff.size() > nSkipHanumSize) && (result_size < 4) )
                                {
                                    free(value);
                                } else
                                // Make use of value
                                if (value) {
                                    std::string modValue = boost::replace_all_copy(std::string((const char*)value), "\n", " ");

                                    diaNumber = client->getDiaNumber();//client->getDiaNumber();
#ifdef USE_REDIS_POOL
                                    if ( useRedis ) {
                                        int64_t zCount=0;
                                        std::string sJsonValue;

                                        size_t in_size, out_size;
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
                                                d.AddMember("SPK", rapidjson::Value("R", alloc).Move(), alloc);
                                                d.AddMember("POS_START", client->rx_sframe/10, alloc);
                                                d.AddMember("POS_END", client->rx_eframe/10, alloc);
                                                d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                                rapidjson::StringBuffer strbuf;
                                                rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                                d.Accept(writer);

                                                sJsonValue = strbuf.GetString();

                                                if ( bUseRemSpaceInNumwords )
                                                {
                                                    remSpaceInSentence( sJsonValue );
                                                }

                                                #ifdef USE_FIND_KEYWORD
                                                if ( bUseFindKeyword )
                                                {
                                                    for(klIter = keywordList.begin(); klIter != keywordList.end(); klIter++ )
                                                    {
                                                        if ( sJsonValue.find(*klIter) != std::string::npos )
                                                        {
                                                            client->m_Logger->debug("VRClient::thrdRxProcess(%s) - Find Keyword(%s)", client->m_sCallId.c_str(), (*klIter).c_str());
                                                            break;
                                                        }
                                                    }
                                                }
                                                #endif

                                            }

                                            vVal.push_back(toString(diaNumber));
                                            vVal.push_back(sJsonValue);

                                            // for TEST
                                            if (bSaveJsonData && client->m_deliver) {
                                                client->m_deliver->insertJsonData(fhCallId, sJsonValue, (diaNumber==1)?0:1, 0, 0, client->m_sCounselCode);
                                            }

                                            if ( bSendDataRedis )
                                            {
                                                if ( !xRedis.zadd(dbi, redisKey, vVal, zCount) ) {
                                                    client->m_Logger->error("VRClient::thrdRxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str(), dbi.GetErrInfo(), zCount);
                                                }
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

                                    // to STTDeliver(file)
                                    if (client->m_deliver) {
                                        client->m_deliver->insertSTT(fhCallId, modValue, item->spkNo, client->rx_sframe/10, client->rx_eframe/10, client->m_sCounselCode);
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
                                vBuff.push_back(buf[i]);

                            }
                            client->rx_sframe = client->rx_eframe;

                            if (client->tx_hold) {
                                client->tx_hold = 0;
                            }

                        }
                        else
                        {
                            // and clear buff, set msg header
                            vBuff.clear();

                            for(size_t i=0; i<nHeadLen; i++) {
                                vBuff.push_back(buf[i]);

                            }
                            client->rx_sframe = client->rx_eframe;

                            if (client->tx_hold) {
                                client->tx_hold = 0;
                            }
                        }
                    }
                    
                    posBuf += framelen;

                    before_vadres = vadres;
                }

                } // only_record

				if (!item->flag) {	// 호가 종료되었음을 알리는 flag, 채널 갯수와 flag(0)이 들어온 갯수를 비교해야한다.
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
                    if (!bOnlySil && bUseSavePcm)
                    {
                        FILE *pPcm;

                        std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_r.pcm");
                        pPcm = fopen(tempPcmFile.c_str(), "wb");
                        if (pPcm)
                        {
                            fwrite((const void*)&vBuff[nHeadLen], sizeof(char), vBuff.size()-nHeadLen, pPcm);
                            fclose(pPcm);
                        }
                    }
                    #endif
                    value= gearman_client_do(gearClient, fname, NULL, 
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
                            diaNumber = client->getDiaNumber();
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
                                        d.AddMember("SPK", rapidjson::Value("R", alloc).Move(), alloc);
                                        d.AddMember("POS_START", client->rx_sframe/10, alloc);
                                        d.AddMember("POS_END", client->rx_eframe/10, alloc);
                                        d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                        rapidjson::StringBuffer strbuf;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                        d.Accept(writer);

                                        sJsonValue = strbuf.GetString();

                                        if ( bUseRemSpaceInNumwords )
                                        {
                                            remSpaceInSentence( sJsonValue );
                                        }

                                        #ifdef USE_FIND_KEYWORD
                                        if ( bUseFindKeyword ) 
                                        {
                                            for(klIter = keywordList.begin(); klIter != keywordList.end(); klIter++ )
                                            {
                                                if ( sJsonValue.find(*klIter) != std::string::npos )
                                                {
                                                    client->m_Logger->debug("VRClient::thrdRxProcess(%s) - Find Keyword(%s)", (*klIter).c_str());
                                                    break;
                                                }
                                            }
                                        }
                                        #endif

                                    }

                                    vVal.push_back(toString(diaNumber));
                                    vVal.push_back(sJsonValue);

                                    // for TEST
                                    if (bSaveJsonData && client->m_deliver) {
                                        client->m_deliver->insertJsonData(fhCallId, sJsonValue, (diaNumber==1)?0:1, 0, 0, client->m_sCounselCode);
                                    }

                                    if ( bSendDataRedis )
                                    {
                                        if ( !xRedis.zadd(dbi, redisKey, vVal, zCount) ) {
                                            client->m_Logger->error("VRClient::thrdRxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str(), dbi.GetErrInfo(), zCount);
                                        }
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

                            // to STTDeliver(file)
                            if (client->m_deliver) {
                                client->m_deliver->insertSTT(fhCallId, modValue, item->spkNo, client->rx_sframe/10, client->rx_eframe/10, client->m_sCounselCode);
                            }
                            
                        }
                    }
                    else if (gearman_failed(rc)){
                        client->m_Logger->error("VRClient::thrdRxProcess(%s) - failed gearman_client_do(). [%d : %d], timeout(%d)", client->m_sCallId.c_str(), client->rx_sframe, client->rx_eframe, client->m_nGearTimeout);
                    }

                    } // only_record

                    // and clear buff, set msg header
                    vBuff.clear();

                    delete item;

                    if (client->m_is_save_pcm) {

                        wHdr.Riff.ChunkSize = totalVoiceDataLen + sizeof(WAVE_HEADER) - 8;
                        wHdr.Data.ChunkSize = totalVoiceDataLen;

                        pcmFile.open(filename, ios::in | ios::out | ios::binary);
                        if (pcmFile.is_open()) {
                            pcmFile.seekp(0);
                            pcmFile.write((const char*)&wHdr, sizeof(WAVE_HEADER));
                            pcmFile.close();
                        }
                    }

                    client->RxState =0;
                    client->syncBreak = 1;
                    client->m_Logger->debug("VRClient::thrdRxProcess(%s) - FINISH CALL.(%d)", client->m_sCallId.c_str(), client->RxState);
                    break;
				}

				delete item;
			}

			std::this_thread::sleep_for(std::chrono::microseconds(10));//milliseconds(1));
		}
        
        sprintf(client->ServerName,"%s", svr_nm.c_str());
        client->TotalVoiceDataLen = totalVoiceDataLen;
        client->m_Logger->debug("VRClient::thrdRxProcess(%s) - ServerName(%s), TotalVoiceDataLen(%d)", client->m_sCallId.c_str(), svr_nm.c_str(), totalVoiceDataLen);

        fvad_free(vad);

	}

    if ( !bOnlyRecord ) gearman_client_free(gearClient);

#ifdef USE_REDIS_POOL
    if ( useRedis ) 
        iconv_close(it);
#endif

    client->m_Logger->debug("VRClient::thrdRxProcess(%s) - RxState(%d)", client->m_sCallId.c_str(), client->RxState);

}

void VRClient::thrdTxProcess(VRClient* client) {

	QueItem* item;
    gearman_client_st *gearClient = nullptr;
    gearman_return_t ret;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;
    PosPair stPos;
    WAVE_HEADER wHdr;
    
    char buf[BUFLEN];
    uint16_t nHeadLen=0;
    int chkRealSize=0;
    
    uint8_t *vpBuf = NULL;
    size_t posBuf = 0;
    std::vector<uint8_t> vBuff;
    uint64_t totalVoiceDataLen;
    size_t framelen;
    std::string svr_nm;
    char fname[64];

    #ifdef USE_FIND_KEYWORD
    std::list< std::string > keywordList;
    std::list< std::string >::iterator klIter;
    #endif


    std::string fhCallId;
    char timebuff [32];
    char datebuff[32];
    struct tm timeinfo;
    std::string fullpath;
    std::string filename;
    std::ofstream pcmFile;
    bool bOnlyRecord = !config->getConfig("stas.only_record", "false").compare("true");
    int nMinVBuffSize = config->getConfig("stas.min_buff_size", 10000);

#ifdef EN_SAVE_PCM
    bool bOnlySil;
    bool bUseSavePcm;
    std::string pcmFilename;
#endif

    bool bUseSkipHanum = !config->getConfig("stas.use_skip_hanum", "false").compare("true");
    unsigned int nSkipHanumSize = config->getConfig("stas.skip_hanum_buff_size", 16000);

    bool bUseFindKeyword = !config->getConfig("stas.use_find_keyword", "fasle").compare("true");
    bool bUseRemSpaceInNumwords = !config->getConfig("stas.use_rem_space_numwords", "false").compare("true");
    bool bSaveJsonData = !config->getConfig("stas.save_json_data", "false").compare("true");

    localtime_r(&client->m_tStart, &timeinfo);
    strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",&timeinfo);
    strftime (datebuff,sizeof(datebuff),"%Y%m%d",&timeinfo);

    fullpath = client->m_pcm_path + "/" + datebuff + "/" + client->m_sCounselCode + "/";
    filename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + std::string("_l.wav");
#ifdef EN_SAVE_PCM
    bOnlySil = !config->getConfig("stas.only_silence", "false").compare("true");
    bUseSavePcm = !config->getConfig("stas.use_save_pcm", "false").compare("true");

    if ( (bOnlySil || bUseSavePcm) && config->isSet("stas.pcm_path") && (config->getConfig("stas.pcm_path", "").size() > 0) )
    {
        pcmFilename = config->getConfig("stas.pcm_path", client->m_pcm_path.c_str()) + "/" + datebuff + "/" + client->m_sCounselCode + "/";
        MakeDirectory(pcmFilename.c_str());
        pcmFilename = pcmFilename + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
    }
    else pcmFilename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
#endif

    vBuff.reserve(MM_SIZE);

    MakeDirectory(fullpath.c_str());
    
    framelen = client->m_framelen * 2;

    sprintf(fname, "%s", client->m_sFname.c_str());

    fhCallId = std::string(timebuff) + "_" + client->m_sCallId;

#ifdef USE_REDIS_POOL
    bool useRedis = !config->getConfig("redis.use", "false").compare("true");
    bool bSendDataRedis = !config->getConfig("redis.send_rt_stt", "false").compare("true");
    iconv_t it = NULL;
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

#ifdef USE_FIND_KEYWORD
    if ( bUseFindKeyword )
    {
        keywordList = DBHandler::getKeywords();
    }
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
        client->m_Logger->error("VRClient::thrdTxProcess() - ERROR (Failed gearman_client_create - %s)", client->m_sCallId.c_str());

        client->TxState =0;
        return;
    }
    
    ret= gearman_client_add_server(gearClient, client->m_sGearHost.c_str(), client->m_nGearPort);
    if (gearman_failed(ret))
    {
        client->m_Logger->error("VRClient::thrdTxProcess() - ERROR (Failed gearman_client_add_server - %s)", client->m_sCallId.c_str());

        client->TxState =0;
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
        if (!vad) {
            client->m_Logger->error("VRClient::thrdTxProcess() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
            if ( !bOnlyRecord ) gearman_client_free(gearClient);
            client->TxState =0;
            return;
        }
        fvad_set_sample_rate(vad, 8000);
        fvad_set_mode(vad, client->m_mode);

		// 실시간의 경우 통화가 종료되기 전까지 Queue에서 입력 데이터를 받아 처리
		// FILE인 경우 기존과 동일하게 filename을 전달하는 방법 이용
        if (client->m_nGearTimeout) {
            if ( !bOnlyRecord ) gearman_client_set_timeout(gearClient, client->m_nGearTimeout);
        }
        
        // write wav heaer to file(mmap);
        vBuff.clear();
        client->tx_hold = 0;
        aDianum = 0;
        totalVoiceDataLen = 0;
        svr_nm = "DEFAULT";

        vadres = before_vadres = 0;
		while (client->TxState)
		{
			while (!client->m_qTXQue.empty()) {
				item = client->m_qTXQue.front();
				client->m_qTXQue.pop();

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
                        continue;
                    }

                    if (vadres > 0) {
                        // 직전 버퍼 값을 사용... 인식률 향상 확인용
                        if (posBuf && (vBuff.size() == nHeadLen))
                        {
                            vpBuf = (uint8_t *)(item->voiceData+posBuf-framelen);
                            for(size_t i=0; i<framelen; i++) {
                                vBuff.push_back(vpBuf[i]);
                            }
                            vpBuf = (uint8_t *)(item->voiceData+posBuf);
                        }
                        for(size_t i=0; i<framelen; i++) {
                            vBuff.push_back(vpBuf[i]);
                            
                        }
                    }
                    
                    if (!vadres && (vBuff.size()<=nHeadLen)) {
                        // start ms
                        client->tx_sframe = client->tx_eframe - (client->m_framelen/8);
                    }

                    if (
                        client->rx_hold ||
                        (!vadres && (vBuff.size()>nHeadLen))) {
                        chkRealSize = checkRealSize(vBuff, nHeadLen, framelen, client->m_framelen);

                        if ( chkRealSize > nMinVBuffSize ) {   // 8000 bytes, 0.5 이하의 음성데이터는 처리하지 않음

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

                            #ifdef EN_SAVE_PCM
                            if (!bOnlySil && bUseSavePcm)
                            {
                                FILE *pPcm;

                                std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_l.pcm");
                                pPcm = fopen(tempPcmFile.c_str(), "wb");
                                if (pPcm)
                                {
                                    fwrite((const void*)&vBuff[nHeadLen], sizeof(char), vBuff.size()-nHeadLen, pPcm);
                                    fclose(pPcm);
                                }
                            }
                            #endif
                            value= gearman_client_do(gearClient, fname, NULL, 
                                                            (const void*)&vBuff[0], vBuff.size(),
                                                            &result_size, &rc);
                                                            
                            aDianum++;
                            
                            if (gearman_success(rc))
                            {
                                #ifdef EN_SAVE_PCM
                                // 의미없는 잡음이라 예상되는 음성데이터만 pcm으로 저장
                                if (bOnlySil && (result_size < 4))
                                {
                                    FILE *pPcm;

                                    std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_l.pcm");
                                    pPcm = fopen(tempPcmFile.c_str(), "wb");
                                    if (pPcm)
                                    {
                                        fwrite((const void*)&vBuff[nHeadLen], sizeof(char), vBuff.size()-nHeadLen, pPcm);
                                        fclose(pPcm);
                                    }
                                }
                                #endif

                                // n초 이상 음성데이터에 대해 네, 예 등과 같이 한음절 응답이 경우 무시
                                if ( bUseSkipHanum && value && (vBuff.size() > nSkipHanumSize) && (result_size < 4) )
                                {
                                    free(value);
                                } else
                                // Make use of value
                                if (value) {
                                    std::string modValue = boost::replace_all_copy(std::string((const char*)value), "\n", " ");
                                    diaNumber = client->getDiaNumber();
#ifdef USE_REDIS_POOL
                                    if ( useRedis ) {
                                        int64_t zCount=0;
                                        std::string sJsonValue;

                                        size_t in_size, out_size;
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
                                                d.AddMember("SPK", rapidjson::Value("L", alloc).Move(), alloc);
                                                d.AddMember("POS_START", client->tx_sframe/10, alloc);
                                                d.AddMember("POS_END", client->tx_eframe/10, alloc);
                                                d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                                rapidjson::StringBuffer strbuf;
                                                rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                                d.Accept(writer);

                                                sJsonValue = strbuf.GetString();

                                                if ( bUseRemSpaceInNumwords )
                                                {
                                                    remSpaceInSentence( sJsonValue );
                                                }

                                                #ifdef USE_FIND_KEYWORD
                                                if ( bUseFindKeyword ) 
                                                {
                                                    for(klIter = keywordList.begin(); klIter != keywordList.end(); klIter++ )
                                                    {
                                                        if ( sJsonValue.find(*klIter) != std::string::npos )
                                                        {
                                                            client->m_Logger->debug("VRClient::thrdTxProcess(%s) - Find Keyword(%s)", client->m_sCallId.c_str(), (*klIter).c_str());
                                                            break;
                                                        }
                                                    }
                                                }
                                                #endif
                                            }

                                            vVal.push_back(toString(diaNumber));
                                            vVal.push_back(sJsonValue);

                                            // for TEST
                                            if (bSaveJsonData && client->m_deliver) {
                                                client->m_deliver->insertJsonData(fhCallId, sJsonValue, (diaNumber==1)?0:1, 0, 0, client->m_sCounselCode);
                                            }

                                            if ( bSendDataRedis )
                                            {
                                                if ( !xRedis.zadd(dbi, redisKey, vVal, zCount) ) {
                                                    client->m_Logger->error("VRClient::thrdTxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str(), dbi.GetErrInfo(), zCount);
                                                }
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

                                    // to STTDeliver(file)
                                    if (client->m_deliver) {
                                        client->m_deliver->insertSTT(fhCallId, modValue, item->spkNo, client->tx_sframe/10, client->tx_eframe/10, client->m_sCounselCode);
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
                                vBuff.push_back(buf[i]);

                            }
                            client->tx_sframe = client->tx_eframe;

                            if (client->rx_hold) {
                                client->rx_hold = 0;
                            }

                        }
                        else
                        {
                            // and clear buff, set msg header
                            vBuff.clear();

                            for(size_t i=0; i<nHeadLen; i++) {
                                vBuff.push_back(buf[i]);

                            }
                            client->tx_sframe = client->tx_eframe;

                            if (client->rx_hold) {
                                client->rx_hold = 0;
                            }
                        }
                    }
                    
                    posBuf += framelen;

                    before_vadres = vadres;
                }

                } // only_record

				if (!item->flag) {	// 호가 종료되었음을 알리는 flag, 채널 갯수와 flag(0)이 들어온 갯수를 비교해야한다.
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
                    if (!bOnlySil && bUseSavePcm)
                    {
                        FILE *pPcm;

                        std::string tempPcmFile = pcmFilename + std::to_string(aDianum) + std::string("_l.pcm");
                        pPcm = fopen(tempPcmFile.c_str(), "wb");
                        if (pPcm)
                        {
                            fwrite((const void*)&vBuff[nHeadLen], sizeof(char), vBuff.size()-nHeadLen, pPcm);
                            fclose(pPcm);
                        }
                    }
                    #endif
                    value= gearman_client_do(gearClient, fname, NULL, 
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
                            diaNumber = client->getDiaNumber();
#ifdef USE_REDIS_POOL
                            if ( useRedis ) {
                                int64_t zCount=0;
                                std::string sJsonValue;
                                size_t in_size, out_size;
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
                                        d.AddMember("SPK", rapidjson::Value("L", alloc).Move(), alloc);
                                        d.AddMember("POS_START", client->tx_sframe/10, alloc);
                                        d.AddMember("POS_END", client->tx_eframe/10, alloc);
                                        d.AddMember("VALUE", rapidjson::Value(utf_buf, alloc).Move(), alloc);

                                        rapidjson::StringBuffer strbuf;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
                                        d.Accept(writer);

                                        sJsonValue = strbuf.GetString();

                                        if ( bUseRemSpaceInNumwords )
                                        {
                                            remSpaceInSentence( sJsonValue );
                                        }

                                        #ifdef USE_FIND_KEYWORD
                                        if ( bUseFindKeyword ) 
                                        {
                                            for(klIter = keywordList.begin(); klIter != keywordList.end(); klIter++ )
                                            {
                                                if ( sJsonValue.find(*klIter) != std::string::npos )
                                                {
                                                    client->m_Logger->debug("VRClient::thrdTxProcess(%s) - Find Keyword(%s)", client->m_sCallId.c_str(), (*klIter).c_str());
                                                    break;
                                                }
                                            }
                                        }
                                        #endif

                                    }

                                    vVal.push_back(toString(diaNumber));
                                    vVal.push_back(sJsonValue);

                                    // for TEST
                                    if (bSaveJsonData && client->m_deliver) {
                                        client->m_deliver->insertJsonData(fhCallId, sJsonValue, (diaNumber==1)?0:1, 0, 0, client->m_sCounselCode);
                                    }

                                    if ( bSendDataRedis )
                                    {
                                        if ( !xRedis.zadd(dbi, redisKey, vVal, zCount) ) {
                                            client->m_Logger->error("VRClient::thrdTxProcess(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str(), dbi.GetErrInfo(), zCount);
                                        }
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

                            // to STTDeliver(file)
                            if (client->m_deliver) {
                                client->m_deliver->insertSTT(fhCallId, modValue, item->spkNo, client->tx_sframe/10, client->tx_eframe/10, client->m_sCounselCode);
                            }
                            
                        }
                    }
                    else if (gearman_failed(rc)){
                        client->m_Logger->error("VRClient::thrdTxProcess(%s) - failed gearman_client_do(). [%d : %d], timeout(%d)", client->m_sCallId.c_str(), client->tx_sframe, client->tx_eframe, client->m_nGearTimeout);
                    }

                    } // only_record

                    // and clear buff, set msg header
                    vBuff.clear();

                    delete item;

                    if (client->m_is_save_pcm) {

                        wHdr.Riff.ChunkSize = totalVoiceDataLen + sizeof(WAVE_HEADER) - 8;
                        wHdr.Data.ChunkSize = totalVoiceDataLen;

                        pcmFile.open(filename, ios::in | ios::out | ios::binary);
                        if (pcmFile.is_open()) {
                            pcmFile.seekp(0);
                            pcmFile.write((const char*)&wHdr, sizeof(WAVE_HEADER));
                            pcmFile.close();
                        }
                    }
                    client->TxState=0;
                    client->syncBreak = 1;
                    client->m_Logger->debug("VRClient::thrdTxProcess(%s) - FINISH CALL.(%d)", client->m_sCallId.c_str(), client->TxState);
                    break;
				}

				delete item;
				// 예외 발생 시 처리 내용 : VDCManager의 removeVDC를 호출할 수 있어야 한다. - 이 후 VRClient는 item->flag(0)에 대해서만 처리한다.
			}

			std::this_thread::sleep_for(std::chrono::microseconds(10));
		}
        
        client->m_Logger->debug("VRClient::thrdTxProcess(%s) - ServerName(%s), TotalVoiceDataLen(%d)", client->m_sCallId.c_str(), svr_nm.c_str(), totalVoiceDataLen);

        fvad_free(vad);

	}

    if ( !bOnlyRecord ) gearman_client_free(gearClient);

#ifdef USE_REDIS_POOL
    if ( useRedis )
        iconv_close(it);
#endif

    client->m_Logger->debug("VRClient::thrdTxProcess(%s) - TxState(%d)", client->m_sCallId.c_str(), client->TxState);

}

void VRClient::insertQueItem(QueItem* item)
{
	std::lock_guard<std::mutex> g(m_mxQue);

    if (item->spkNo == 1)
        m_qRXQue.push(item);
    else if (item->spkNo == 2)
        m_qTXQue.push(item);
    else {
        delete item;
    }
}

#ifdef USE_REDIS_POOL
xRedisClient& VRClient::getXRdedisClient()
{
    return RedisHandler::instance()->getRedisClient();
}
#endif


uint32_t VRClient::getDiaNumber()
{
    std::lock_guard<std::mutex> g(m_mxDianum);
    return ++DiaNumber;
}




#endif

#endif // ENABLE_REALTIME
