
#include "stas.h"
#include "Notifier.h"
#include "VFCManager.h"

#include "DBHandler.h"

#include "HAManager.h"

#include <sys/inotify.h>
#include <unistd.h>

/* According to POSIX.1-2001, POSIX.1-2008 */
#include <sys/select.h>

/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>

#include <log4cpp/Category.hh>

#include <chrono>
#include <cstring>
#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

Notifier* Notifier::m_instance = nullptr;



Notifier::Notifier(VFCManager *vfcm, DBHandler *DBHandler)
: m_vfcm(vfcm), m_DBHandler(DBHandler), m_LiveFlag(true)
{
    
}

Notifier::~Notifier()
{
    if (m_thrdNoti.joinable()) m_thrdNoti.join();
}

Notifier* Notifier::instance(VFCManager *vfcm, DBHandler *DBHandler)
{
    if (!m_instance) {
        m_instance = new Notifier(vfcm, DBHandler);
    }
    
    return m_instance;
}

int Notifier::startWork()
{
    log4cpp::Category *logger = config->getLogger();
    
	if (!config->isSet("notify.input_path")) {
		logger->fatal("Not set inotify.input_path");
		return 1;
	}

	std::shared_ptr<std::string> path = std::make_shared<std::string>(config->getConfig("notify.input_path","/home/stt/Smart-VR/NOTI"));
	logger->info("Initialize monitoring module to watch %s", path->c_str());
	if (!itfact::common::checkPath(*path.get(), true)) {
		logger->error("Cannot create directory '%s' with error %s", path->c_str(), std::strerror(errno));
		return 1;
	}

    m_thrdNoti = std::thread(Notifier::thrdFunc, this);
    
    return 0;
}


#define BUF_LEN (10 * (sizeof(struct inotify_event) + FILENAME_MAX + 1))

void Notifier::thrdFunc(Notifier *noti)
{
    struct timeval tv;
    fd_set rfds;
    int selVal;
	char buf[BUF_LEN] __attribute__ ((aligned(8)));
    log4cpp::Category *logger = config->getLogger();
    HAManager *ham = HAManager::getInstance();
	std::shared_ptr<std::string> path = std::make_shared<std::string>(config->getConfig("notify.input_path", "/home/stt/Smart-VR/NOTI"));
    std::string downpath = "";

	if (!config->isSet("notify.down_path")) {
        downpath = "file://";
        downpath += *path.get();
    }
    else {
        downpath = config->getConfig("notify.down_path", "file:///home/stt/Smart-VR/input");
    }
    
	int inotify = inotify_init();
	if (inotify < 0) {
		int rc = errno;
		logger->error("Cannot initialization iNotify '%s'", std::strerror(rc));
		return;
	}

	int wd = inotify_add_watch(inotify, path->c_str(), IN_CLOSE_WRITE);
	if (wd < 0) {
		int rc = errno;
		logger->error("Cannot watch '%s' with error %s", path->c_str(), std::strerror(rc));
		return;
	}

	std::string watch_ext = config->getConfig("notify.watch", "txt");

    if (ham && !ham->getHAStat()) {
        logger->debug("Notifier::thrdFunc() - Waiting... for Standby Mode");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    while(noti->m_LiveFlag) {
		tv.tv_sec = 0;
		tv.tv_usec = 500000;
		FD_ZERO(&rfds);
		FD_SET(inotify, &rfds);

		selVal = select(inotify+1, &rfds, NULL, NULL, &tv);

        if (selVal > 0) {
            ssize_t numRead = read(inotify, buf, BUF_LEN);
            if (numRead <= 0) {
                int rc = errno;
                if (rc == EINTR)
                    continue;

                logger->warn("Error occurred: (%d), %s", rc, std::strerror(rc));
                break;
            }

            struct inotify_event *event = NULL;
            for (char *p = buf; p < buf + numRead; p += sizeof(struct inotify_event) + event->len) {
                event = (struct inotify_event *) p;
                if (!(event->mask & IN_ISDIR)) {
                    // Call Job
                    std::shared_ptr<std::string> filename = std::make_shared<std::string>(event->name, event->len);
                    std::string file_ext = filename->substr(filename->rfind(".") + 1);

                    if (filename->at(0) != '.' && file_ext.find(watch_ext) == 0 &&
                        (file_ext.size() == watch_ext.size() || file_ext.at(watch_ext.size()) == '\0' )) {

                        logger->debug("Noti file %s (Watch: '%s', ext: '%s')", filename->c_str(), watch_ext.c_str(), file_ext.c_str());

                        try {
                            // option값에 따라 동작이 바뀌어야 한다.
                            // pushItem() 시 protocol 추가해야한다. - FILE, MOUNT, HTTP, HTTPS, FTP, FTPS, SFTP, SCP, SSH
                            if (config->getConfig("notify.index_type", "list").compare("filename") == 0) {

                                // download path, uri, filename, call_id에 대한 좀 더 명확한 정의가 필요하다.
                                #ifdef EXCEPT_EXT
                                if (noti->m_DBHandler->searchTaskInfo(downpath, *filename.get(), std::string(""))) continue;
                                noti->m_DBHandler->insertTaskInfo(downpath, *filename.get(), std::string(""));
                                #else
                                if (noti->m_DBHandler->searchTaskInfo(downpath, *filename.get(), filename->substr(0, filename->rfind(".")))) continue;
                                noti->m_DBHandler->insertTaskInfo(downpath, *filename.get(), filename->substr(0, filename->rfind(".")));
                                #endif

                            }
                            else {
                                std::string pathfile = *path.get()+"/"+*filename.get();
                                std::ifstream index_file(pathfile);
                                std::vector<std::string> v;
                                logger->debug("open file %s", pathfile.c_str());
                                if (index_file.is_open()) {
                                    for (std::string line; std::getline(index_file, line); ) {
                                        if (line.empty() || line.size() < 5)
                                            continue;

                                        boost::split(v, line, boost::is_any_of(",  \t"), boost::token_compress_on);

                                        logger->debug("line %s (v.size: %d)", line.c_str(), v.size());

                                        if (v.size() > 1) {
                                            if (!noti->m_DBHandler->searchTaskInfo(downpath, v[0], v[1])) {
                                                logger->debug("insert to STT_TBL_JOB_INFO(%s)", v[0].c_str());
                                                noti->m_DBHandler->insertTaskInfo(downpath, v[0], v[1]);
                                            }
                                        }
                                        else {
                                            #ifdef EXCEPT_EXT
                                            if (!noti->m_DBHandler->searchTaskInfo(downpath, v[0], v[0])) 
                                                noti->m_DBHandler->insertTaskInfo(downpath, v[0], v[0]);
                                            #else
                                            if (!noti->m_DBHandler->searchTaskInfo(downpath, v[0], v[0].substr(0, v[0].rfind(".")))) 
                                                noti->m_DBHandler->insertTaskInfo(downpath, v[0], v[0].substr(0, v[0].rfind(".")));
                                            #endif
                                        }

                                    }
                                    index_file.close();
                                }
                            }

                            bool delete_on_list = !config->getConfig("notify.delete_on_list", "false").compare("true");
                            if (delete_on_list)
                                std::remove(filename->c_str());

                        } catch (std::exception &e) {
                            logger->warn("%s: %s", e.what(), filename->c_str());
                        }
                    } else {
                        logger->debug("Ignore %s (Watch: '%s', ext: '%s')", filename->c_str(),
                                        watch_ext.c_str(), file_ext.c_str());
                    }
                }
            }
        }
    }

	inotify_rm_watch(inotify, wd);
}
