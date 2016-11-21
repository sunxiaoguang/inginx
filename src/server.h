#ifndef __INGNIX_SERVER_H__
#define __INGNIX_SERVER_H__

#include <stdint.h>

#include "inginx.h"
#include "networking.h"
#include "anet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct inginxServer
{
  inginxClient *current;
  list *clients;
  list *pending;
  list *closing;
  list *listening;
  volatile int32_t shutdown;
  aeEventLoop *el;
  inginxLogger logger;
  inginxLogLevel loggerLevel;
  void *loggerData;
  inginxListener listener;
  int32_t listenerMask;
  void *listenerData;
  volatile time_t unixTime;
  int64_t msTime;
  int64_t hz;
  int64_t maxIdleTime;
  int32_t cronLoops;
  char error[ANET_ERR_LEN];
  int32_t groupSize;
  inginxServer *group;
  pthread_t dispatchingThread;
  http_parser_execute parser;
} inginxServer;

void inginxServerClientRequest(inginxServer *inginxServer, inginxClient *client);
void inginxServerClientDisconnected(inginxServer *inginxServer, inginxClient *client);
void inginxServerClientDestroyed(inginxServer *inginxServer, inginxClient *client);

#ifdef __GNUC__
void inginxServerLog(inginxServer *inginxServer, inginxLogLevel level, const char *func, const char *file, uint32_t line, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
#else
void inginxServerLog(inginxServer *inginxServer, inginxLogLevel level, const char *func, const char *file, uint32_t line, const char *fmt, ...);
#endif
void inginxServerLogFlush(inginxServer *server);
int32_t inginxServerIsDispatchingThread(inginxServer *server);

#define INGINX_LOG_TRACE_ENABLED(SRV) ((SRV)->loggerLevel <= INGINX_LOG_LEVEL_TRACE)
#define INGINX_LOG_DEBUG_ENABLED(SRV) ((SRV)->loggerLevel <= INGINX_LOG_LEVEL_DEBUG)
#define INGINX_LOG_INFO_ENABLED(SRV) ((SRV)->loggerLevel <= INGINX_LOG_LEVEL_INFO)
#define INGINX_LOG_WARN_ENABLED(SRV) ((SRV)->loggerLevel <= INGINX_LOG_LEVEL_WARN)
#define INGINX_LOG_ERROR_ENABLED(SRV) ((SRV)->loggerLevel <= INGINX_LOG_LEVEL_ERROR)
#define INGINX_LOG_FATAL_ENABLED(SRV) ((SRV)->loggerLevel <= INGINX_LOG_LEVEL_FATAL)

#define INGINX_LOG_TRACE(SRV, ...)                                                                    \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_TRACE_ENABLED(stmp)) {                                                             \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_TRACE, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
    }                                                                                                 \
  } while (0)

#define INGINX_LOG_DEBUG(SRV, ...)                                                                    \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_DEBUG_ENABLED(stmp)) {                                                             \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_DEBUG, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
    }                                                                                                 \
  } while (0)

#define INGINX_LOG_INFO(SRV, ...)                                                                     \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_INFO_ENABLED(stmp)) {                                                              \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_INFO, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);    \
    }                                                                                                 \
  } while (0)

#define INGINX_LOG_WARN(SRV, ...)                                                                     \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_WARN_ENABLED(stmp)) {                                                              \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_WARN, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);    \
    }                                                                                                 \
  } while (0)

#define INGINX_LOG_ERROR(SRV, ...)                                                                    \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_ERROR_ENABLED(stmp)) {                                                             \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_ERROR, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
    }                                                                                                 \
  } while (0)

#define INGINX_LOG_FATAL(SRV, ...)                                                                    \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_FATAL_ENABLED(stmp)) {                                                             \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_FATAL, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
    }                                                                                                 \
  } while (0)

#define INGINX_LOG_ASSERT(SRV, ...)                                                                   \
  do {                                                                                                \
    struct inginxServer *stmp = (SRV);                                                                \
    if (INGINX_LOG_ASSERT_ENABLED(stmp)) {                                                            \
      inginxServerLog(stmp, INGINX_LOG_LEVEL_ASSERT, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);  \
    }                                                                                                 \
  } while (0)

#define INGINX_ABORT(SRV) { inginxServerLogFlush((SRV)); abort(); }

#ifdef _DEBUG
#define INGINX_ASSERT(SRV, X)                           \
{                                                       \
  if(!(X)) {                                            \
    struct inginxServer *stmp = (SRV);                  \
    LOG_ASSERT(stmp, "Assertion Failed: %s", #X);       \
    INGINX_ABORT(stmp)                                  \
  }                                                     \
}

#define INGINX_VERIFY(SRV, X)                           \
{                                                       \
  if(!(X)) {                                            \
    struct inginxServer *stmp = (SRV);                  \
    LOG_ASSERT(stmp, "Verification Failed: %s", #X);    \
    INGINX_ABORT(stmp)                                  \
  }                                                     \
}

#else
#define INGINX_ASSERT(SRV, X) while (0) { if ((X)) {} }

#define INGINX_VERIFY(SRV, X)                       \
{                                                   \
  if (!(X)) {                                       \
    struct inginxServer *stmp = (SRV);              \
    LOG_ASSERT(stmp, "Verification Failed: %s", #X);\
  }                                                 \
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* */
