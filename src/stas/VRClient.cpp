#ifdef  ENABLE_REALTIME
#ifndef USE_REALTIME_MT

#include "VRClient.h"
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

// VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, /*log4cpp::Category *logger,*/ DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen)
// 	: m_sGearHost(gearHost), m_nGearPort(gearPort), m_nGearTimeout(gearTimeout), m_sFname(fname), m_sCallId(callid), m_sCounselCode(counselcode), m_nLiveFlag(1), m_cJobType(jobType), m_nNumofChannel(noc), m_deliver(deliver), /*m_Logger(logger),*/ m_s2d(s2d), m_is_save_pcm(is_save_pcm), m_pcm_path(pcm_path), m_framelen(framelen*8)
#ifdef EN_RINGBACK_LEN
VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT, uint32_t ringbacklen)
#else
VRClient::VRClient(VRCManager* mgr, string& gearHost, uint16_t gearPort, int gearTimeout, string& fname, string& callid, string& counselcode, uint8_t jobType, uint8_t noc, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode, time_t startT)
#endif
	: m_sGearHost(gearHost), m_nGearPort(gearPort), m_nGearTimeout(gearTimeout), m_sFname(fname), m_sCallId(callid), m_sCounselCode(counselcode), m_nLiveFlag(1), m_cJobType(jobType), m_nNumofChannel(noc), m_deliver(deliver), m_s2d(s2d), m_is_save_pcm(is_save_pcm), m_pcm_path(pcm_path), m_framelen(framelen*8), m_mode(mode)
{
	m_Mgr = mgr;
    m_tStart = startT;
	m_thrd = std::thread(VRClient::thrdMain, this);
    m_thrd.detach();
#ifdef EN_RINGBACK_LEN
    m_nRingbackLen = ringbacklen;
#endif
	//printf("\t[DEBUG] VRClinet Constructed.\n");
    m_Logger = config->getLogger();
    m_Logger->debug("VRClinet Constructed(%s)(%s)(%d).", m_sCallId.c_str(), m_sFname.c_str(), m_mode);
}


VRClient::~VRClient()
{
	QueItem* item;
	while (!m_qRTQue.empty()) {
		item = m_qRTQue.front();
		m_qRTQue.pop();

		// delete[] item->voiceData;
		delete item;
	}

	//printf("\t[DEBUG] VRClinet Destructed.\n");
    m_Logger->debug("VRClinet Destructed. CALLID(%s), CS_CD(%s)", m_sCallId.c_str(), m_sCounselCode.c_str());
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

	QueItem* item;
	std::lock_guard<std::mutex> *g;
    gearman_client_st *gearClient;
    gearman_return_t ret;
    void *value = NULL;
    size_t result_size;
    gearman_return_t rc;
    PosPair stPos;
    std::vector< PosPair > vPos;
    WAVE_HEADER wHdr[2];
#ifndef FAD_FUNC    
    char* pEndpos=NULL;
    std::size_t start,end;
#endif
    
    char buf[BUFLEN];
    uint16_t nHeadLen=0;
    int chkRealSize=0;

    std::string fhCallId;
    char timebuff [32];
    char datebuff[32];
    struct tm timeinfo;
    std::string fullpath;// = client->m_pcm_path + "/"  + datebuff + "/" + client->m_sCounselCode + "/";
    std::string filename;// = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId;// + std::string("_l.wav");
    std::ofstream pcmFile;
    bool bOnlyRecord = !config->getConfig("stas.only_record", "false").compare("true");

    #ifdef USE_FIND_KEYWORD
    std::list< std::string > keywordList;
    std::list< std::string >::iterator klIter;
    #endif


    char fname[64];
    uint64_t totalVLen=0;

    bool useDelCallInfo = !config->getConfig("stas.use_del_callinfo", "false").compare("true");
    int nDelSecs = config->getConfig("stas.del_secs", 0);
    int nMinVBuffSize = config->getConfig("stas.min_buff_size", 10000);
    // int nMaxWaitNo = config->getConfig("stas.max_wait_no", 7);
    // int nCurrWaitNo = 0;
#ifdef EN_SAVE_PCM
    bool bOnlySil;
    bool bUseSavePcm;// = !config->getConfig("stas.use_save_pcm", "false").compare("true");
    std::string pcmFilename;// = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId + "_";
#endif

    bool bUseSkipHanum = !config->getConfig("stas.use_skip_hanum", "false").compare("true");
    int nSkipHanumSize = config->getConfig("stas.skip_hanum_buff_size", 16000);

    bool bUseFindKeyword = !config->getConfig("stas.use_find_keyword", "fasle").compare("true");
    bool bUseRemSpaceInNumwords = !config->getConfig("stas.use_rem_space_numwords", "false").compare("true");

    bool bSaveJsonData = !config->getConfig("stas.save_json_data", "false").compare("true");

#ifdef FAD_FUNC
    uint8_t *vpBuf = NULL;
    size_t posBuf = 0;
    std::vector<uint8_t> vBuff[2];
    size_t sframe[2];
    size_t eframe[2];
    uint64_t totalVoiceDataLen[2];
    size_t framelen;

    vBuff[0].reserve(MM_SIZE);
    vBuff[1].reserve(MM_SIZE);
    
    framelen = client->m_framelen * 2;
#endif // FAD_FUNC

    localtime_r(&client->m_tStart, &timeinfo);
    strftime (timebuff,sizeof(timebuff),"%Y%m%d%H%M%S",&timeinfo);
    strftime (datebuff,sizeof(datebuff),"%Y%m%d",&timeinfo);

    fullpath = client->m_pcm_path + "/"  + datebuff + "/" + client->m_sCounselCode + "/";
    filename = fullpath + client->m_sCounselCode + "_" + timebuff + "_" + client->m_sCallId;// + std::string("_l.wav");
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

    fhCallId = std::string(timebuff) + "_" + client->m_sCallId;
    MakeDirectory(fullpath.c_str());

#ifdef USE_REDIS_POOL
    bool useRedis = !config->getConfig("redis.use", "false").compare("true");
    bool bSendDataRedis = !config->getConfig("redis.send_rt_stt", "false").compare("true");
    iconv_t it;
    VALUES vVal;
    std::string sPubCannel = config->getConfig("redis.pubchannel", "RT-STT");
    xRedisClient &xRedis = client->getXRdedisClient();
    RedisDBIdx dbi(&xRedis);
    std::string redisKey = "G_CS:";
    char redisValue[256];
    std::string strRedisValue;

    if ( useRedis ) {
        dbi.CreateDBIndex(client->getCallId().c_str(), APHash, CACHE_TYPE_1);
        it = iconv_open("UTF-8", "EUC-KR");

        // 2019-01-10, 호 시작 시 상담원 상태 변경 전달 - 호 시작
        int64_t zCount=0;
        redisKey.append(client->getCounselCode());

        //  {"REG_DTM":"10:15", "STATE":"E", "CALL_ID":"CALL011"}
        strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",&timeinfo);
        sprintf(redisValue, "{\"REG_DTM\":\"%s\", \"STATE\":\"I\", \"CALL_ID\":\"%s\"}", timebuff, client->getCallId().c_str());
        strRedisValue = redisValue;
        
        // xRedis.hset( dbi, redisKey, client->getCallId(), strRedisValue, zCount );
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

    sprintf(fname, "%s", client->m_sFname.c_str());

#ifdef USE_FIND_KEYWORD
    keywordList = DBHandler::getKeywords();
#endif

    for(int i=0; i<2; i++) {
        //memset(wHdr[i], 0, sizeof(WAVE_HEADER));
        memcpy(wHdr[i].Riff.ChunkID, "RIFF", 4);
        wHdr[i].Riff.ChunkSize = 0;
        memcpy(wHdr[i].Riff.Format, "WAVE", 4);

        memcpy(wHdr[i].Fmt.ChunkID, "fmt ", 4);
        wHdr[i].Fmt.ChunkSize = 16;
        wHdr[i].Fmt.AudioFormat = 1;
        wHdr[i].Fmt.NumChannels = 1;
        wHdr[i].Fmt.SampleRate = 8000;
        wHdr[i].Fmt.AvgByteRate = 8000 * 1 * 16 / 8 ;
        wHdr[i].Fmt.BlockAlign = 1 * 16 / 8;
        wHdr[i].Fmt.BitPerSample = 16;

        memcpy(wHdr[i].Data.ChunkID, "data", 4);
        wHdr[i].Data.ChunkSize = 0;

        if (client->m_is_save_pcm) {
            std::string spker = (i == 0)?std::string("_r.wav"):std::string("_l.wav");
            // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_") + /*std::to_string(client->m_nNumofChannel)*/spker + std::string(".wav");
            // std::ofstream pcmFile;
            std::string l_filename = filename + spker;

            pcmFile.open(l_filename, ios::out | ios::trunc | ios::binary);
            if (pcmFile.is_open()) {
                pcmFile.write((const char*)&wHdr[i], sizeof(WAVE_HEADER));
                pcmFile.close();
            }
        }
    }
    
    for (int i=0; i<client->m_nNumofChannel; i++) {
        stPos.bpos = 0;
        stPos.epos = 0;
        vPos.push_back( stPos );
    }
    
    if ( !bOnlyRecord ) {

    gearClient = gearman_client_create(NULL);
    if (!gearClient) {
        //printf("\t[DEBUG] VRClient::thrdMain() - ERROR (Failed gearman_client_create - %s)\n", client->m_sCallId.c_str());
        client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed gearman_client_create - %s)", client->m_sCallId.c_str());

        WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        // client->m_thrd.detach();
        delete client;

#ifdef USE_REDIS_POOL
        if ( useRedis )
            iconv_close(it);
#endif
        return;
    }
    
    ret= gearman_client_add_server(gearClient, client->m_sGearHost.c_str(), client->m_nGearPort);
    if (gearman_failed(ret))
    {
        //printf("\t[DEBUG] VRClient::thrdMain() - ERROR (Failed gearman_client_add_server - %s)\n", client->m_sCallId.c_str());
        client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed gearman_client_add_server - %s)", client->m_sCallId.c_str());

        WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

        // client->m_thrd.detach();
        delete client;

#ifdef USE_REDIS_POOL
        if ( useRedis )
            iconv_close(it);
#endif
        return;
    }

    } // only_record
    

	// m_cJobType에 따라 작업 형태를 달리해야 한다. 
	if (client->m_cJobType == 'R') {
        uint32_t diaNumber=1;   // DB 실시간 STT 테이블에 저장될 호(Call)단위 Index 값
#ifndef FAD_FUNC
        const char* srcBuff;
        const char* dstBuff;
        uint32_t srcLen, dstLen;
        string tmpStt[2];
        uint32_t sttIdx;
        
        tmpStt[0] = "";
        tmpStt[1] = "";
#else
        Fvad *vad = NULL;
        int vadres;
        int aDianum[2];

        vad = fvad_new();
        if (!vad) {//} || (fvad_set_sample_rate(vad, in_info.samplerate) < 0)) {
            client->m_Logger->error("VRClient::thrdMain() - ERROR (Failed fvad_new(%s))", client->m_sCallId.c_str());
            if ( !bOnlyRecord ) gearman_client_free(gearClient);
            WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);
            // client->m_thrd.detach();
            delete client;

#ifdef USE_REDIS_POOL
            if ( useRedis ) 
                iconv_close(it);
#endif
            return;
        }
        fvad_set_sample_rate(vad, 8000);
        fvad_set_mode(vad, client->m_mode);

#endif // FAD_FUNC

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

#ifdef FAD_FUNC
        // write wav heaer to file(mmap);
        vBuff[0].clear();
        vBuff[1].clear();

#ifdef EN_RINGBACK_LEN
        sframe[0] = eframe[0] = client->m_nRingbackLen;
        sframe[1] = eframe[1] = client->m_nRingbackLen;
#else
        sframe[0] = 0;
        sframe[1] = 0;
        eframe[0] = 0;
        eframe[1] = 0;
#endif
        
        aDianum[0] = 0;
        aDianum[1] = 0;

        totalVoiceDataLen[0] = 0;
        totalVoiceDataLen[1] = 0;
#endif
        auto t1 = std::chrono::high_resolution_clock::now();
            
		while (client->m_nLiveFlag)
		{
			while (!client->m_qRTQue.empty()) {
				// g = new std::lock_guard<std::mutex>(client->m_mxQue);
				item = client->m_qRTQue.front();
				client->m_qRTQue.pop();
				// delete g;

                totalVoiceDataLen[item->spkNo-1] += item->lenVoiceData;

                vPos[item->spkNo -1].epos += item->lenVoiceData;
				// queue에서 가져온 item을 STT 하는 로직을 아래에 코딩한다.
				// call-id + item->spkNo => call-id for rt-stt
                memset(buf, 0, sizeof(buf));
                if (!item->flag) {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "LAST");
                }
                else if (item->flag == 2) {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "FIRS");
                    t1 = std::chrono::high_resolution_clock::now();
                }
                else {
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "NOLA");
                }
                nHeadLen = strlen(buf);

#ifdef FAD_FUNC     
                if (vBuff[item->spkNo-1].size()>0) {
                    for(int i=0; i<nHeadLen; i++) {
                        vBuff[item->spkNo-1][i] = buf[i];
                    }
                }
                else {
                    for(int i=0; i<nHeadLen; i++) {
                        vBuff[item->spkNo-1].push_back(buf[i]);
                    }
                }
                
#else
                
                memcpy(buf+nHeadLen, (const void*)item->voiceData, item->lenVoiceData);
                
#endif // FAD_FUNC

                if (client->m_is_save_pcm) {
                    std::string spker = (item->spkNo == 1)?std::string("_r.wav"):std::string("_l.wav");
                    // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_") + /*std::to_string(client->m_nNumofChannel)*/spker + std::string(".wav");
                    // std::ofstream pcmFile;
                    std::string l_filename = filename + spker;

                    pcmFile.open(l_filename, ios::out | ios::app | ios::binary);
                    if (pcmFile.is_open()) {
                        pcmFile.write((const char*)item->voiceData, item->lenVoiceData);
                        pcmFile.close();
                    }
                }

                if ( !bOnlyRecord ) {
                
#ifdef FAD_FUNC
                // check vad!, by loop()
                // if finish check vad and vBuff is no empty, send buff to VR by gearman
                // vadres == 1 vBuff[item->spkNo-1].push_back();
                // vadres == 0 and vBuff[item->spkNo-1].size() > 0 then send buff to gearman
                posBuf = 0;
                while ((item->lenVoiceData >= framelen) && ((item->lenVoiceData - posBuf) >= framelen)) {
                    vpBuf = (uint8_t *)(item->voiceData+posBuf);
                    eframe[item->spkNo-1] += (client->m_framelen/8);
                    // Convert the read samples to int16
                    vadres = fvad_process(vad, (const int16_t *)vpBuf, client->m_framelen);

                    //client->m_Logger->debug("VRClient::thrdMain(%s) - SUB WHILE... [%d : %d], timeout(%d)", client->m_sCallId.c_str(), sframe[item->spkNo -1], eframe[item->spkNo -1], client->m_nGearTimeout);

                    if (vadres < 0) {
                        //client->m_Logger->error("VRClient::thrdMain(%d, %d, %s)(%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", nHeadLen, item->spkNo, buf, client->m_sCallId.c_str(), vBuff[item->spkNo-1].size(), sframe[item->spkNo-1], eframe[item->spkNo-1]);
                        continue;
                    }

                    if (vadres > 0) {
                        // 직전 버퍼 값을 사용... 인식률 향상 확인용
                        if (posBuf && (vBuff[item->spkNo-1].size() == nHeadLen))
                        {
                            vpBuf = (uint8_t *)(item->voiceData+posBuf-framelen);
                            for(size_t i=0; i<framelen; i++) {
                                vBuff[item->spkNo-1].push_back(vpBuf[i]);
                            }
                            vpBuf = (uint8_t *)(item->voiceData+posBuf);
                        }
                        for(size_t i=0; i<framelen; i++) {
                            vBuff[item->spkNo-1].push_back(vpBuf[i]);
                            
                        }
                    }
                    
                    if (!vadres && (vBuff[item->spkNo-1].size()<=nHeadLen)) {
                        // start ms
                        sframe[item->spkNo-1] = eframe[item->spkNo-1] - 20;
                    }

                    if (!vadres && (vBuff[item->spkNo-1].size()>nHeadLen)) {
                        chkRealSize = checkRealSize(vBuff[item->spkNo-1], nHeadLen, framelen, client->m_framelen);
                        // client->m_Logger->debug("VRClient::thrdMain(%s) - SPK(%d), orgSize(%d), checkRealSize(%d)", client->m_sCallId.c_str(), item->spkNo, vBuff[item->spkNo-1].size(), chkRealSize);
                        // if ( (nCurrWaitNo > nMaxWaitNo) || (vBuff[item->spkNo-1].size() > nMinVBuffSize)) {   // 3200 bytes, 0.2초 이하의 음성데이터는 처리하지 않음
                        if ( /*vBuff[item->spkNo-1].size()*/chkRealSize > nMinVBuffSize ) {   // 3200 bytes, 0.2초 이하의 음성데이터는 처리하지 않음
                            // send buff to gearman
                            if (aDianum[item->spkNo-1] == 0) {
                                sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "FIRS");
                            }
                            else {
                                sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "NOLA");
                            }
                            for(size_t i=0; i<strlen(buf); i++) {
                                vBuff[item->spkNo-1][i] = buf[i];
                            }
                            //client->m_Logger->debug("VRClient::thrdMain(%d, %d, %s)(%s) - send buffer buff_len(%lu), spos(%lu), epos(%lu)", nHeadLen, item->spkNo, buf, client->m_sCallId.c_str(), vBuff[item->spkNo-1].size(), sframe[item->spkNo-1], eframe[item->spkNo-1]);
                            #ifdef EN_SAVE_PCM
                            if (!bOnlySil && bUseSavePcm)
                            {
                                FILE *pPcm;

                                std::string tempPcmFile = pcmFilename + std::to_string(aDianum[item->spkNo-1]) + ((item->spkNo == 1)?std::string("_r.pcm"):std::string("_l.pcm"));
                                pPcm = fopen(tempPcmFile.c_str(), "wb");
                                if (pPcm)
                                {
                                    fwrite((const void*)&vBuff[item->spkNo-1][nHeadLen], sizeof(char), vBuff[item->spkNo-1].size()-nHeadLen, pPcm);
                                    fclose(pPcm);
                                }
                            }
                            #endif
                            value= gearman_client_do(gearClient, fname/*"vr_realtime"*//*client->m_sFname.c_str()*/, NULL, 
                                                            (const void*)&vBuff[item->spkNo-1][0], vBuff[item->spkNo-1].size(),
                                                            &result_size, &rc);
                            
                            aDianum[item->spkNo-1]++;

                            if (gearman_success(rc))
                            {
                                #ifdef EN_SAVE_PCM
                                if (bOnlySil && (result_size < 4))
                                {
                                    FILE *pPcm;

                                    std::string tempPcmFile = pcmFilename + std::to_string(aDianum[item->spkNo-1]) + ((item->spkNo == 1)?std::string("_r.pcm"):std::string("_l.pcm"));
                                    pPcm = fopen(tempPcmFile.c_str(), "wb");
                                    if (pPcm)
                                    {
                                        fwrite((const void*)&vBuff[item->spkNo-1][nHeadLen], sizeof(char), vBuff[item->spkNo-1].size()-nHeadLen, pPcm);
                                        fclose(pPcm);
                                    }
                                }
                                #endif

                                // n초 이상 음성데이터에 대해 네, 예 등과 같이 한음절 응답이 경우 무시
                                if ( bUseSkipHanum && value && (vBuff[item->spkNo-1].size() > nSkipHanumSize) && (result_size < 4) )
                                {
                                    free(value);
                                } else
                                // Make use of value
                                if (value) {
                                    std::string modValue = boost::replace_all_copy(std::string((const char*)value), "\n", " ");
                                    // std::cout << "DEBUG : value(" << (char *)value << ") : size(" << result_size << ")" << std::endl;
                                    //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);
                                                            
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
                                                // d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                                d.AddMember("SPK", rapidjson::Value((item->spkNo==1)?"R":"L", alloc).Move(), alloc);
                                                d.AddMember("POS_START", sframe[item->spkNo -1]/10, alloc);
                                                d.AddMember("POS_END", eframe[item->spkNo -1]/10, alloc);
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
                                                            client->m_Logger->debug("VRClient::thrdMain(%s) - Find Keyword(%s)", client->m_sCallId.c_str(), (*klIter).c_str());
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
                                                if ( !xRedis.zadd(dbi, redisKey/*client->getCallId()*/, vVal, zCount) ) {
                                                    client->m_Logger->error("VRClient::thrdMain(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str()/*client->m_sCallId.c_str()*/, dbi.GetErrInfo(), zCount);
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
                                        client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, sframe[item->spkNo -1]/10, eframe[item->spkNo -1]/10, modValue/*boost::replace_all_copy(std::string((const char*)value), "\n", " ")*/);
                                    }
#endif // DISABLE_ON_REALTIME
                                    //STTDeliver::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                                    // to STTDeliver(file)
                                    if (client->m_deliver) {
                                        client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue/*boost::replace_all_copy(std::string((const char*)value), "\n", " ")*/, item->spkNo, sframe[item->spkNo -1]/10, eframe[item->spkNo -1]/10, client->m_sCounselCode);
                                    }

                                    free(value);
                                    
                                    diaNumber++;
                                }
                            }
                            else if (gearman_failed(rc)){
                                client->m_Logger->error("VRClient::thrdMain(%s) - failed gearman_client_do(). [%lu : %lu], timeout(%d)", client->m_sCallId.c_str(), sframe[item->spkNo -1], eframe[item->spkNo -1], client->m_nGearTimeout);
                            }


                            // and clear buff, set msg header
                            vBuff[item->spkNo-1].clear();

                            for(size_t i=0; i<nHeadLen; i++) {
                                //vBuff[item->spkNo-1][i] = buf[i];
                                vBuff[item->spkNo-1].push_back(buf[i]);

                            }
                            sframe[item->spkNo-1] = eframe[item->spkNo-1];

                            // nCurrWaitNo = 0;
                        }
                        else
                        {
                            // and clear buff, set msg header
                            vBuff[item->spkNo-1].clear();

                            for(size_t i=0; i<nHeadLen; i++) {
                                //vBuff[item->spkNo-1][i] = buf[i];
                                vBuff[item->spkNo-1].push_back(buf[i]);

                            }
                            sframe[item->spkNo-1] = eframe[item->spkNo-1];
                            // nCurrWaitNo++;
                        }
                    }
                    
                    posBuf += framelen;
                }

                } // only_record

#else // FAD_FUNC

                value= gearman_client_do(gearClient, client->m_sFname.c_str(), NULL, 
                                                (const void*)buf, (nHeadLen + item->lenVoiceData),
                                                &result_size, &rc);
                if (gearman_success(rc))
                {
                    // Make use of value
                    if (value) {
                        // std::cout << "DEBUG : value(" << (char *)value << ") : size(" << result_size << ")" << std::endl;
                        pEndpos = strchr((char*)value, '|');
                        if (pEndpos) {
                            sscanf(pEndpos, "|%lu|%lu", &start, &end);
                            //client->m_Logger->debug("VRClient::thrdMain(%s) - start_pos(%lu), end_pos(%lu).", client->m_sCallId.c_str(), start, end);
                            *pEndpos = 0;
                        }
#if 1
                        sttIdx = 0;
                        srcBuff = tmpStt[item->spkNo-1].c_str();
                        srcLen = strlen(srcBuff);
                        dstBuff = (const char*)value;
                        dstLen = strlen(dstBuff);
                        //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);

                        if (srcLen <= dstLen) {
                            for(sttIdx=0; sttIdx<srcLen; sttIdx++) {
                                if (!memcmp(srcBuff, dstBuff, srcLen-sttIdx)) {
                                    break;
                                }
                            }
                            sttIdx = srcLen-sttIdx;
                            while(sttIdx) {
                                if ((dstBuff[sttIdx] == ' ') || (dstBuff[sttIdx] == '\n')) {
                                    sttIdx++;
                                    break;
                                }
                                sttIdx--;
                            }
                            
                        }

                        //client->m_Logger->debug("VRClient::thrdMain(%s) - sttIdx(%d)\nsrc(%s)\ndst(%s)", client->m_sCallId.c_str(), sttIdx, srcBuff, dstBuff);

                        if ((!sttIdx || (sttIdx < dstLen)) && strlen(dstBuff+sttIdx)) {
                            std::string modValue = boost::replace_all_copy(std::string((const char*)dstBuff+sttIdx), "\n", " ");

                            // to DB
                            if (client->m_s2d) {
                                client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, pEndpos ? start : vPos[item->spkNo -1].bpos/160, pEndpos ? end : vPos[item->spkNo -1].epos/160, modValue/*boost::replace_all_copy(std::string((const char*)dstBuff+sttIdx), "\n", " ")*/);
                            }
                            //FileHandler::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                            // to FileHandler(file)
                            if (client->m_deliver) {
                                client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue/*boost::replace_all_copy(std::string((const char*)dstBuff+sttIdx), "\n", " ")*/, item->spkNo, pEndpos ? start : vPos[item->spkNo -1].bpos/160, pEndpos ? end : vPos[item->spkNo -1].epos/160, client->m_sCounselCode);
                            }
                            
                        }

                        tmpStt[item->spkNo-1].clear();
                        tmpStt[item->spkNo-1] = (const char*)value;

#else
                        // to DB
                        if (client->m_s2d) {
                            client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, pEndpos ? start : vPos[item->spkNo -1].bpos/160, pEndpos ? end : vPos[item->spkNo -1].epos/160, std::string((const char*)value));
                        }
                        //FileHandler::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                        // to FileHandler(file)
                        if (client->m_deliver) {
                            client->m_deliver->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, pEndpos ? start : vPos[item->spkNo -1].bpos/160, pEndpos ? end : vPos[item->spkNo -1].epos/160);
                        }
#endif
                        free(value);
                        
                        diaNumber++;
                    }
                }
                else if (gearman_failed(rc)){
                    client->m_Logger->error("VRClient::thrdMain(%s) - failed gearman_client_do(). [%lu : %lu], timeout(%d)", client->m_sCallId.c_str(), vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos, client->m_nGearTimeout);
                }
                
                vPos[item->spkNo -1].bpos = vPos[item->spkNo -1].epos + 1;

#endif  // FAD_FUNC

				if (!item->flag) {	// 호가 종료되었음을 알리는 flag, 채널 갯수와 flag(0)이 들어온 갯수를 비교해야한다.
                    std::string svr_nm = "DEFAULT";
					//printf("\t[DEBUG] VRClient::thrdMain(%s) - final item delivered.\n", client->m_sCallId.c_str());
                    client->m_Logger->debug("VRClient::thrdMain(%s, %d) - final item delivered.", client->m_sCallId.c_str(), item->spkNo);

                    if ( !bOnlyRecord ) {

#ifdef FAD_FUNC
                    // send buff to gearman
                    sprintf(buf, "%s_%d|%s|", client->m_sCallId.c_str(), item->spkNo, "LAST");
                    if (vBuff[item->spkNo-1].size() > 0) {
                        for(size_t i=0; i<strlen(buf); i++) {
                            vBuff[item->spkNo-1][i] = buf[i];
                        }
                    }
                    else {
                        for(size_t i=0; i<strlen(buf); i++) {
                            vBuff[item->spkNo-1].push_back(buf[i]);
                        }
                    }
                    #ifdef EN_SAVE_PCM
                    if (!bOnlySil && bUseSavePcm)
                    {
                        FILE *pPcm;

                        std::string tempPcmFile = pcmFilename + std::to_string(aDianum[item->spkNo-1]) + ((item->spkNo == 1)?std::string("_r.pcm"):std::string("_l.pcm"));
                        pPcm = fopen(tempPcmFile.c_str(), "wb");
                        if (pPcm)
                        {
                            fwrite((const void*)&vBuff[item->spkNo-1][nHeadLen], sizeof(char), vBuff[item->spkNo-1].size()-nHeadLen, pPcm);
                            fclose(pPcm);
                        }
                    }
                    #endif
                    value= gearman_client_do(gearClient, fname/*"vr_realtime"*//*client->m_sFname.c_str()*/, NULL, 
                                                    (const void*)&vBuff[item->spkNo-1][0], vBuff[item->spkNo-1].size(),
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
                                        // d.AddMember("CALL_ID", rapidjson::Value(client->getCallId().c_str(), alloc).Move(), alloc);
                                        d.AddMember("SPK", rapidjson::Value((item->spkNo==1)?"R":"L", alloc).Move(), alloc);
                                        d.AddMember("POS_START", sframe[item->spkNo -1]/10, alloc);
                                        d.AddMember("POS_END", eframe[item->spkNo -1]/10, alloc);
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
                                                    client->m_Logger->debug("VRClient::thrdMain(%s) - Find Keyword(%s)", client->m_sCallId.c_str(), (*klIter).c_str());
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
                                        if ( !xRedis.zadd(dbi, redisKey/*client->getCallId()*/, vVal, zCount) ) {
                                            client->m_Logger->error("VRClient::thrdMain(%s) - redis zadd(). [%s], zCount(%d)", redisKey.c_str()/*client->m_sCallId.c_str()*/, dbi.GetErrInfo(), zCount);
                                        }
                                    }

                                    vVal.clear();

                                    free(utf_buf);
                                }
                            }
#endif

#ifdef DISABLE_ON_REALTIME
                            if (client->m_s2d) {
                                client->m_s2d->insertSTTData(diaNumber, client->m_sCallId, item->spkNo, sframe[item->spkNo -1]/10, eframe[item->spkNo -1]/10, modValue/*boost::replace_all_copy(std::string((const char*)value), "\n", " ")*/);
                            }
#endif // DISABLE_ON_REALTIME

                            //STTDeliver::instance(client->m_Logger)->insertSTT(client->m_sCallId, std::string((const char*)value), item->spkNo, vPos[item->spkNo -1].bpos, vPos[item->spkNo -1].epos);
                            // to STTDeliver(file)
                            if (client->m_deliver) {
                                client->m_deliver->insertSTT(fhCallId/*client->m_sCallId*/, modValue/*boost::replace_all_copy(std::string((const char*)value), "\n", " ")*/, item->spkNo, sframe[item->spkNo -1]/10, eframe[item->spkNo -1]/10, client->m_sCounselCode);
                            }
                            
                            diaNumber++;
                        }
                    }
                    else if (gearman_failed(rc)){
                        client->m_Logger->error("VRClient::thrdMain(%s) - failed gearman_client_do(). [%d : %d], timeout(%d)", client->m_sCallId.c_str(), sframe[item->spkNo -1], eframe[item->spkNo -1], client->m_nGearTimeout);
                    }

                    // and clear buff, set msg header
                    vBuff[item->spkNo-1].clear();

#endif

                    } // only_record

					if (!(--client->m_nNumofChannel)) {
                        totalVLen = totalVoiceDataLen[item->spkNo-1];

						client->m_Mgr->removeVRC(client->m_sCallId);

						// if ( item->voiceData != NULL ) delete[] item->voiceData;
						delete item;

#ifdef USE_REDIS_POOL
                        if ( useRedis ) {
                            int64_t zCount=0;
                            time_t t;
                            struct tm *tmp;

                            t = time(NULL);
                            tmp = localtime(&t);

                            if ( bSendDataRedis )
                            {
                                if (!xRedis.publish(dbi, sPubCannel.c_str(), client->getCallId().c_str(), zCount)) {
                                    client->m_Logger->error("VRClient::thrdMain(%s) - redis publish(). [%s], zCount(%d)", client->m_sCallId.c_str(), dbi.GetErrInfo(), zCount);
                                }
                            }

                            redisKey = "G_CS:";
                            redisKey.append(client->getCounselCode());

                            //  {"REG_DTM":"10:15", "STATE":"E", "CALL_ID":"CALL011"}
                            strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",tmp);
                            sprintf(redisValue, "{\"REG_DTM\":\"%s\", \"STATE\":\"E\", \"CALL_ID\":\"%s\"}", timebuff, client->getCallId().c_str());
                            strRedisValue = redisValue;
                            
                            // xRedis.hset( dbi, redisKey, client->getCallId(), strRedisValue, zCount );
                            if ( bSendDataRedis )
                            {
                                xRedis.lset( dbi, redisKey, 0, strRedisValue );
                            }

                        }
#endif
                        // for TEST
                        if (bSaveJsonData && client->m_deliver) {
                            std::string emptyStr("");
                            client->m_deliver->insertJsonData(fhCallId, emptyStr, 2, 0, 0, client->m_sCounselCode);
                        }

                        if (client->m_s2d) {
                            auto t2 = std::chrono::high_resolution_clock::now();
                            // char timebuff [32];
                            // struct tm * timeinfo = localtime(&client->m_tStart);
                            strftime (timebuff,sizeof(timebuff),"%Y-%m-%d %H:%M:%S",&timeinfo);

                            client->m_s2d->updateCallInfo(client->m_sCallId, true);
                            if ( !useDelCallInfo || ( totalVLen/16000 > nDelSecs ) ) {
                                client->m_s2d->updateTaskInfo(client->m_sCallId, std::string(timebuff), std::string("MN"), client->m_sCounselCode, 'Y', totalVLen, totalVLen/16000, std::chrono::duration_cast<std::chrono::seconds>(t2-t1).count(), 0, "STT_TBL_JOB_INFO", "", svr_nm.c_str());
                            }
                        }
#if 0
                        HAManager::getInstance()->deleteSyncItem(client->m_sCallId);
#else
                        // HA
                        if (HAManager::getInstance())
#ifdef EN_RINGBACK_LEN
                            HAManager::getInstance()->insertSyncItem(false, client->m_sCallId, client->m_sCounselCode, std::string("remove"), 1, 1, 0);
#else
                            HAManager::getInstance()->insertSyncItem(false, client->m_sCallId, client->m_sCounselCode, std::string("remove"), 1, 1);
#endif

#endif
                        if (client->m_is_save_pcm) {
                            for (int i=0; i<2; i++) {
                                std::string spker = (i == 0)?std::string("_r.wav"):std::string("_l.wav");
                                // std::string filename = client->m_pcm_path + "/" + client->m_sCallId + std::string("_") + /*std::to_string(client->m_nNumofChannel)*/spker + std::string(".wav");
                                // std::ofstream pcmFile;
                                std::string l_filename = filename + spker;

                                wHdr[i].Riff.ChunkSize = totalVoiceDataLen[i] + sizeof(WAVE_HEADER) - 8;
                                wHdr[i].Data.ChunkSize = totalVoiceDataLen[i];

                                pcmFile.open(l_filename, ios::in | ios::out /*| ios::ate */| ios::binary);
                                if (pcmFile.is_open()) {
                                    pcmFile.seekp(0);
                                    pcmFile.write((const char*)&wHdr[i], sizeof(WAVE_HEADER));
                                    pcmFile.close();
                                }
                            }
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
                                // job_log->debug("[%s, 0x%X] %s", job_name, THREAD_ID, cmd.c_str());
                                if (std::system(cmd.c_str())) {
                                    client->m_Logger->error("VRClient::thrdMain(%s) Fail to merge wavs: command(%s)", client->m_sCallId.c_str(), cmd.c_str());
                                }
                            }
                        }
                        client->m_Logger->debug("VRClient::thrdMain(%s) - FINISH CALL.", client->m_sCallId.c_str());
						break;
					}
				}

				// delete[] item->voiceData;
				delete item;
				// 예외 발생 시 처리 내용 : VDCManager의 removeVDC를 호출할 수 있어야 한다. - 이 후 VRClient는 item->flag(0)에 대해서만 처리한다.
			}
            //client->m_Logger->debug("VRClient::thrdMain(%s) - WHILE... [%d : %d], timeout(%d)", client->m_sCallId.c_str(), sframe[item->spkNo -1], eframe[item->spkNo -1], client->m_nGearTimeout);
			std::this_thread::sleep_for(std::chrono::microseconds(10));//milliseconds(1));
		}
        
#ifdef FAD_FUNC
        fvad_free(vad);

        // std::vector<uint8_t>().swap(vBuff[0]);
        // std::vector<uint8_t>().swap(vBuff[1]);
#endif

#if 0 // for DEBUG
		if (client->m_is_save_pcm && pcmFile.is_open()) pcmFile.close();
#endif
	}
	// 파일(배치)를 위한 작업 수행 시
	else {
		client->m_Mgr->removeVRC(client->m_sCallId);
	}

    if ( !bOnlyRecord ) gearman_client_free(gearClient);
    std::vector< PosPair >().swap(vPos);

	WorkTracer::instance()->insertWork(client->m_sCallId, client->m_cJobType, WorkQueItem::PROCTYPE::R_FREE_WORKER);

#ifdef USE_REDIS_POOL
    if ( useRedis )
        iconv_close(it);
#endif
    // 3초 이하 호관련 정보 삭제 - totalVoiceDataLen[i]/16000 < 3 인 호 정보 삭제
    // 3초 이하 호 정보 삭제 - totalVLen/16000 < 3 인경우 호 정보 삭제
    if ( useDelCallInfo && nDelSecs ) {
        if ( useRedis && (totalVLen/16000 <= nDelSecs) ) {
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
        if ( client->m_s2d && (totalVLen/16000 <= nDelSecs) ) {
            client->m_s2d->deleteJobInfo(client->m_sCallId);
        }

        // delete wav files
        if ( client->m_is_save_pcm && (totalVLen/16000 <= nDelSecs) )
        {
            for (int i=0; i<2; i++) {
                std::string spker = (i == 0)?std::string("_r.wav"):std::string("_l.wav");
                std::string l_filename = filename + spker;
                remove( l_filename.c_str() ) ;
            }
        }
    }



	// client->m_thrd.detach();
	delete client;
}

void VRClient::insertQueItem(QueItem* item)
{
	std::lock_guard<std::mutex> g(m_mxQue);
	m_qRTQue.push(item);
}

#ifdef USE_REDIS_POOL
xRedisClient& VRClient::getXRdedisClient()
{
    // return m_Mgr->getRedisClient();
    return RedisHandler::instance()->getRedisClient();
}
#endif







#endif // USE_REALTIME_MT

#endif // ENABLE_REALTIME
