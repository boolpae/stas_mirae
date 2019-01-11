#ifdef  ENABLE_REALTIME

#pragma once

#include <stdint.h>

#include <vector>
#include <map>
#include <mutex>
#include <string>

#ifdef WIN32
#include<winsock2.h>
#else

#define SOCKET int
#define closesocket close

#endif

#include <log4cpp/Category.hh>

using namespace std;

class VRClient;
class FileHandler;
class DBHandler;

class VRCManager
{
	static VRCManager* ms_instance;

	string m_sGearHost;
	uint16_t m_nGearPort;
    int m_GearTimeout;
    
    SOCKET m_nSockGearman;

	map< string, VRClient* > m_mWorkerTable;

	mutable std::mutex m_mxMap;
    
    FileHandler *m_deliver;
    
    log4cpp::Category *m_Logger;
    DBHandler* m_s2d;

	mutable std::mutex m_mxQue;
    
    bool m_is_save_pcm;
    string m_pcm_path;
    size_t m_framelen;
	int m_mode;

	bool m_bOnlyRecord;

public:
	static VRCManager* instance(const std::string gearHostIp, const uint16_t gearHostPort, int geartimout, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode);
	static void release();

#ifdef EN_RINGBACK_LEN
	int16_t requestVRC(string& callid, string& counselcode, time_t &startT, uint8_t jobType, uint8_t noc=1, uint32_t ringbackLen=0);
#else
	int16_t requestVRC(string& callid, string& counselcode, time_t &startT, uint8_t jobType, uint8_t noc=1);
#endif
	void removeVRC(string callid);
	void removeAllVRC();

	void outputVRCStat();

	VRClient* getVRClient(string& callid);

	string& getGearHost() { return m_sGearHost; }

	uint16_t getGearPort() { return m_nGearPort; }
    
#ifdef EN_RINGBACK_LEN
    int addVRC(string callid, string counselcode, string fname, uint8_t jobtype, uint8_t noc, uint32_t ringbacklen);
#else
    int addVRC(string callid, string counselcode, string fname, uint8_t jobtype, uint8_t noc);
#endif


private:
	VRCManager(int geartimeout, FileHandler *deliver, DBHandler* s2d, bool is_save_pcm, string pcm_path, size_t framelen, int mode);
	virtual ~VRCManager();

	bool connectGearman();
    void disconnectGearman();
	bool getGearmanFnames(std::vector< std::string > &vFnames);
	void getFnamesFromString(std::string &gearResult, std::vector< std::string > &vFnames);
	void setGearHost(string host) { m_sGearHost = host; }
	void setGearPort(uint16_t port) { m_nGearPort = port; }
	void getFnamesFromString4MT(std::string & gearResult, std::vector<std::string>& vFnames);

};


#endif // ENABLE_REALTIME
