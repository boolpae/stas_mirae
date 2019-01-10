
#ifndef __NOTIFIER_H__
#define __NOTIFIER_H__


#include <thread>


class VFCManager;
class DBHandler;

class Notifier {
public:
    static Notifier* instance(VFCManager *vfcm, DBHandler *DBHandler);
    ~Notifier();
    int startWork();
    void stopWork() { m_LiveFlag = false; }
    
private:
    Notifier(VFCManager *vfcm, DBHandler *DBHandler);
    static void thrdFunc(Notifier *noti);
    
private:
    static Notifier* m_instance;
    std::thread m_thrdNoti;
    
    VFCManager *m_vfcm;
    DBHandler *m_DBHandler;
    bool m_LiveFlag;
};



#endif // __NOTIFIER_H__