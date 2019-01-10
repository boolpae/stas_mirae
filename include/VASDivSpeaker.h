#ifndef _VASDIVSPEAKER_H_
#define _VASDIVSPEAKER_H_

#include <string>

#include <libgearman/gearman.h>

class DBHandler;
class FileHandler;
class JobInfoItem;

class VASDivSpeaker {
private:
    DBHandler *m_hDB;
    FileHandler *m_hFile;
    JobInfoItem *m_jobItem;

public:
    VASDivSpeaker(DBHandler *db, FileHandler *file, JobInfoItem *item);
    virtual ~VASDivSpeaker();

    int startWork(gearman_client_st *gearClient, std::string &funcname, std::string &unseg);
};

#endif // _VASDIVSPEAKER_H_
