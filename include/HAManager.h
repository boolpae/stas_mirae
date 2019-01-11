#ifndef _HAMANAGER_H_
#define _HAMANAGER_H_

#include <iostream>
#include <map>
#include <queue>
#include <thread>
#include <mutex>


#define LOG4CPP

#ifdef LOG4CPP

#include <log4cpp/Category.hh>

#ifdef ENABLE_REALTIME
class VRCManager;
class VDCManager;
#endif

#endif

class SyncItem {
    public:
    SyncItem(
        bool bSignalType, // true: call start, false: call end
        std::string sCallId,
        std::string sCounselCode,
        std::string sFuncName,
        unsigned short n1port,
        unsigned short n2port
#ifdef EN_RINGBACK_LEN
        ,uint32_t nRingbackLen
#endif
        );
    virtual ~SyncItem();

    bool m_bSignalType; // true: call start, false: call end
    std::string m_sCallId;
    std::string m_sCounselCode;
    std::string m_sFuncName;
    unsigned short m_n1port;
    unsigned short m_n2port;
#ifdef EN_RINGBACK_LEN
    uint32_t m_nRingbackLen;
#endif
};

class HAManager {
    public:
    virtual ~HAManager();

#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
    static HAManager* instance(VRCManager *vrm, VDCManager *vdm/*, log4cpp::Category *logger*/);
#else
    static HAManager* instance();
#endif
#else
    static HAManager* instance();
#endif
    static HAManager* getInstance();
    static void release();

    int init(std::string ipaddr, uint16_t port);

    //int sendCallSignal();
#ifdef EN_RINGBACK_LEN
    int insertSyncItem( bool calltype, std::string callid,  std::string counselcode, std::string funcname, uint16_t port1, uint16_t port2, uint32_t ringbacklen );
#else
    int insertSyncItem( bool calltype, std::string callid,  std::string counselcode, std::string funcname, uint16_t port1, uint16_t port2 );
#endif
    //void deleteSyncItem( std::string callid );

    bool getHAStat() { return m_bStat; }
    
    void deleteSyncItem(std::string callid);
    void outputSignals();

    private:
#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
    HAManager(VRCManager *vrm, VDCManager *vdm/*, log4cpp::Category *logger*/);
#else
    HAManager();
#endif
#else
    HAManager();
#endif

    static void thrdActive(HAManager* mgr);
    static void thrdSender(HAManager* mgr, int sockfd);

    static void thrdStandby(HAManager* mgr, int standbysock);

    private:
    static HAManager* m_instance;

    volatile bool m_bLiveFlag;
    volatile bool m_bSenderFlag;
#ifdef LOG4CPP
#ifdef ENABLE_REALTIME
    VRCManager *m_vrm;
    VDCManager *m_vdm;
#endif
    log4cpp::Category *m_Logger;
#endif
    volatile bool m_bStat;   // true: active, false: standby
    int m_nActiveSock;
    int m_nStandbySock;
    std::string m_sAddr;
    short m_nPort;

    std::map< std::string, SyncItem* > m_mSyncTable;
    std::queue< SyncItem* > m_qSignalQue;
	mutable std::mutex m_mxQue;
};


#endif  // _HAMANAGER_H_
