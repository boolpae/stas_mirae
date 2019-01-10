#ifdef  ENABLE_REALTIME

#ifndef _DIVSPKMANAGER_H_
#define _DIVSPKMANAGER_H_

#include <string>
#include <vector>
#include <mutex>

class DBHandler;


typedef struct _spkitem {
    std::string callid;
    std::string sttvalue;
} SpkItem;

class DivSpkManager {

public:
    static DivSpkManager *instance();
    virtual ~DivSpkManager();

    int doDivSpeaker(std::string callid, std::string &sttValue);
    int insertSttValue(std::string callid, std::string &sttValue);
    int getSttValue(std::string callid, std::string &sttValue);
    void clearSttValue(std::string callid);

private:
    DivSpkManager();

private:
    static DivSpkManager* m_instance;
    DBHandler *m_DBHandler;

    std::vector< SpkItem > m_vSpkItems;
	mutable std::mutex m_mxQue;
};


#endif // _DIVSPKMANAGER_H_


#endif // ENABLE_REALTIME