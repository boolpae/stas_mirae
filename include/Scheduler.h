#ifndef __Scheduler_H__
#define __Scheduler_H__

/* Scheduler for Database
 * 
 */
#include <thread>

class DBHandler;
class VFCManager;

class Scheduler {
public:
    virtual ~Scheduler();
    static Scheduler* instance(DBHandler *sttdb, VFCManager *vfcmgr);
	static void release();

private:
    Scheduler(DBHandler *sttdb, VFCManager *vfcmgr);
    static void thrdFuncScheduler(Scheduler* schd, VFCManager *vfcmgr);
    static void thrdFuncStateUpdate(Scheduler* schd);
    
private:
    static Scheduler* m_instance;
    bool m_bLiveFlag;

    DBHandler *m_sttdb;
    VFCManager *m_vfcmgr;

	std::thread m_thrd;
    std::thread m_thrdUpdater;

};


#endif //__Scheduler_H__