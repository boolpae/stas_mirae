#ifdef USE_REDIS_POOL

#ifndef _REDIS_HANDLER_H_
#define _REDIS_HANDLER_H_

#include "xRedisClient.h"


using namespace xrc;

class RedisHandler
{
public:
    virtual ~RedisHandler();

    static RedisHandler* instance();
	xRedisClient& getRedisClient() { return m_xRedis; }

private:
    RedisHandler();
    static void thrdKeepAlive(RedisHandler *handler);

private:
    static RedisHandler* m_instance;

	xRedisClient m_xRedis;
};



#endif // _REDIS_HANDLER_H_

#endif // USE_REDIS_POOL
