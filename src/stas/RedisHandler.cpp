/*
 * RedisHandler Module
 */
#ifdef USE_REDIS_POOL

#include "RedisHandler.h"
#include "stas.h"

#include <thread>
#include <chrono>

enum {
    CACHE_TYPE_1, 
    CACHE_TYPE_2,
    CACHE_TYPE_MAX,
};

RedisNode redisList[3]=
{
    {0,"127.0.0.1", 6379, "12345", 8, 5, 0},
    {1,"127.0.0.1", 6379, "12345", 8, 5, 0},
    {2,"127.0.0.1", 6379, "12345", 8, 5, 0}
};

RedisHandler* RedisHandler::m_instance = nullptr;

RedisHandler::RedisHandler()
{

}

RedisHandler::~RedisHandler()
{
	m_xRedis.Release();
}

RedisHandler* RedisHandler::instance()
{
    if (m_instance) return m_instance;

    m_instance = new RedisHandler();

	std::string redis_server_ip = config->getConfig("redis.addr", "127.0.0.1");
	std::string redis_auth = config->getConfig("redis.password", "");

	for(int i=0; i<3; i++) {
		redisList[i].host = redis_server_ip.c_str();
		redisList[i].port = config->getConfig("redis.port", 6379);
		redisList[i].passwd = redis_auth.c_str();
		redisList[i].poolsize = config->getConfig("redis.poolsize", 10);
	}

    if ( !config->getConfig("redis.use", "false").compare("true") ) {

        m_instance->m_xRedis.Init(CACHE_TYPE_MAX);
        bool bConn = m_instance->m_xRedis.ConnectRedisCache(redisList, sizeof(redisList) / sizeof(RedisNode), 3, CACHE_TYPE_1);

        if (!bConn) {
            log4cpp::Category *logger = config->getLogger();
            logger->error("RedisHandler::instance() - ERROR (Failed to connect redis server)");
            delete m_instance;
            m_instance = nullptr;
        }

        // KEEPALIVE 호출할 쓰레드 필요... 자체 쓰레드를 이용할 것인지 main 쓰레드를 이용할 것인가?
        std::thread thrd = std::thread(RedisHandler::thrdKeepAlive, m_instance);
        thrd.detach();

    }

    return m_instance;
}

void RedisHandler::thrdKeepAlive(RedisHandler* handler)
{
    while(1) {
        handler->m_xRedis.Keepalive();
        std::this_thread::sleep_for(std::chrono::seconds(100));
    }
}


#endif  // USE_REDIS_POOL
