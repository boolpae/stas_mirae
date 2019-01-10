#pragma once

#include <stdint.h>

#include <map>
#include <mutex>
#include <string>
#include <queue>
#include <thread>

#define SOCKET int
#define closesocket close

#include <log4cpp/Category.hh>

using namespace std;

class VFClient;
class JobInfoItem;

class VFCManager
{
private:
	static VFCManager* m_instance;
    
	string m_sGearHost;
	uint16_t m_nGearPort;
    int m_GearTimeout;
    
    SOCKET m_nSockGearman;

    log4cpp::Category *m_Logger;

	map< string, VFClient* > m_mWorkerTable;
	queue< JobInfoItem* > m_qVFQue;

	mutable std::mutex m_mxMap;
	mutable std::mutex m_mxQue;
    
	static uint64_t m_nVFCs;

	bool m_bMakeMLF;
	string m_sResultPath;
public:
	static VFCManager* instance(const std::string gearHostIp, const uint16_t gearHostPort, int geartimout/*, log4cpp::Category *logger*/);
	static void release();

	void outputVFCStat();

	string& getGearHost() { return m_sGearHost; }
	uint16_t getGearPort() { return m_nGearPort; }
    
    int pushItem(JobInfoItem* item);//std::string line);
    JobInfoItem* popItem();//std::string& line);

	int getAvailableCount();

private:
	VFCManager(int geartimeout/*, log4cpp::Category *logger*/);
	virtual ~VFCManager();

	bool connectGearman();
    void disconnectGearman();
    size_t getWorkerCount();
	size_t getWorkerCountFromString(std::string &gearResult);
	void setGearHost(string host) { m_sGearHost = host; }
	void setGearPort(uint16_t port) { m_nGearPort = port; }
    void syncWorkerVFClient();
    
    static void thrdFuncVFCManager(VFCManager* mgr);
};

