#ifndef __INGINX_H__
#define __INGINX_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct inginxServer inginxServer;
typedef struct inginxClient inginxClient;
typedef struct inginxMessage inginxMessage;
typedef struct inginxClient inginxClient;

typedef enum inginxLogLevel
{
  INGINX_LOG_LEVEL_TRACE = 0,
  INGINX_LOG_LEVEL_DEBUG,
  INGINX_LOG_LEVEL_INFO,
  INGINX_LOG_LEVEL_WARN,
  INGINX_LOG_LEVEL_ERROR,
  INGINX_LOG_LEVEL_FATAL,
  INGINX_LOG_LEVEL_ASSERT,
} inginxLogLevel;

typedef void (*inginxLogger)(inginxServer *s, inginxLogLevel level, const char *func, const char *file, uint32_t line, const char *log, void *opaque);

typedef enum inginxEventType
{
  INGINX_EVENT_TYPE_CONNECTED = 1 << 0,
  INGINX_EVENT_TYPE_DISCONNECTED = 1 << 1,
  INGINX_EVENT_TYPE_REQUEST = 1 << 2,
  INGINX_EVENT_TYPE_RESPONSE = 1 << 3,
  INGINX_EVENT_TYPE_ERROR = 1 << 4,
  INGINX_EVENT_TYPE_DESTROYED = 1 << 5,
  INGINX_EVENT_TYPE_ALL = 0xFFFFFFFF,
} inginxEventType;

typedef void (*inginxListener)(inginxServer *s, inginxClient *c, inginxEventType type, void *eventData, void *opaque);

typedef enum inginxMethod {
  INGINX_METHOD_DELETE = 0,
  INGINX_METHOD_GET = 1,
  INGINX_METHOD_HEAD = 2,
  INGINX_METHOD_POST = 3,
  INGINX_METHOD_PUT = 4,

  INGINX_METHOD_CONNECT = 5,
  INGINX_METHOD_OPTIONS = 6,
  INGINX_METHOD_TRACE = 7,

  INGINX_METHOD_COPY = 8,
  INGINX_METHOD_LOCK = 9,
  INGINX_METHOD_MKCOL = 10,
  INGINX_METHOD_MOVE = 11,
  INGINX_METHOD_PROPFIND = 12,
  INGINX_METHOD_PROPPATCH = 13,
  INGINX_METHOD_SEARCH = 14,
  INGINX_METHOD_UNLOCK = 15,
  INGINX_METHOD_BIND = 16,
  INGINX_METHOD_REBIND = 17,
  INGINX_METHOD_UNBIND = 18,
  INGINX_METHOD_ACL = 19,

  INGINX_METHOD_REPORT = 20,
  INGINX_METHOD_MKACTIVITY = 21,
  INGINX_METHOD_CHECKOUT = 22,
  INGINX_METHOD_MERGE = 23,

  INGINX_METHOD_MSEARCH = 24,
  INGINX_METHOD_NOTIFY = 25,
  INGINX_METHOD_SUBSCRIBE = 26,
  INGINX_METHOD_UNSUBSCRIBE = 27,

  INGINX_METHOD_PATCH = 28,
  INGINX_METHOD_PURGE = 29,

  INGINX_METHOD_MKCALENDAR = 30,
  INGINX_METHOD_LINK = 31,
  INGINX_METHOD_UNLINK = 32
} inginxMethod;

uint16_t inginxMessageStatus(const inginxMessage *message);
inginxMethod inginxMessageMethod(const inginxMessage *message);
const char* inginxMessageUrl(const inginxMessage *message);
const char *inginxMessageHeader(const inginxMessage *message, const char *field);
const char *inginxMessageHeaderNext(const inginxMessage *message, const char *field, const char *cursor);
const char *inginxMessageBody(const inginxMessage *message);
const char *inginxMessageUrlDecoded(const inginxMessage *message);
const char *inginxMessageParameter(const inginxMessage *message, const char *name);
const char *inginxMessageParameterNext(const inginxMessage *message, const char *name, const char *cursor);
void inginxMessageVersion(const inginxMessage *message, uint16_t *major, uint16_t *minor);

const char *inginxVersion(void);

inginxServer *inginxServerCreate(void);
inginxServer *inginxServerHz(inginxServer *server, int32_t hz);
inginxServer *inginxServerGroupCreate(int32_t size, int32_t useProcess);
inginxServer *inginxServerBind(inginxServer *server, const char *address, int32_t backlog);
inginxServer *inginxServerConnectionLimit(inginxServer *server, int32_t limit);
inginxServer *inginxServerMain(inginxServer *server);
inginxServer *inginxServerShutdown(inginxServer *server);
inginxServer *inginxServerLogger(inginxServer *server, inginxLogger logger, inginxLogLevel level, void *opaque);
inginxServer *inginxServerListener(inginxServer *server, inginxListener listener, int32_t mask, void *opaque);
inginxServer *inginxServerStrict(inginxServer *server);
inginxServer *inginxServerRelaxed(inginxServer *server);
void inginxServerSimpleLogger(inginxServer *s, inginxLogLevel level, const char *func, const char *file, uint32_t line, const char *log, void *opaque);
void inginxServerFree(inginxServer *inginxServer);

int32_t inginxClientGetRemoteAddress(inginxClient *client, char *address, size_t size, uint16_t *port);
int32_t inginxClientGetLocalAddress(inginxClient *client, char *address, size_t size, uint16_t *port);
void inginxClientSetStatus(inginxClient *c, int32_t status);
void inginxClientSendError(inginxClient *c, int32_t code);
void inginxClientSendRedirect(inginxClient *c, const char *location);
void inginxClientAddHeader(inginxClient *c, const char *name, const char *value);
void inginxClientAddHeaderVPrintf(inginxClient *c, const char *name, const char *fmt, va_list args);
void inginxClientAddDateHeader(inginxClient *c, const char *name, int64_t date);
void inginxClientAddBody(inginxClient *c, const char *body);
void inginxClientAddBodySize(inginxClient *c, const void *body, size_t size);
void inginxClientAddBodyVPrintf(inginxClient *c, const char *fmt, va_list args);
void inginxClientAddReply(inginxClient *c, const char *data);
void inginxClientAddReplySize(inginxClient *c, const void *data, size_t size);
void inginxClientAddReplyVPrintf(inginxClient *c, const char *fmt, va_list args);

#ifdef __GNUC__
void inginxClientAddHeaderPrintf(inginxClient *c, const char *name, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void inginxClientAddBodyPrintf(inginxClient *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void inginxClientAddReplyPrintf(inginxClient *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#else
void inginxClientAddHeaderPrintf(inginxClient *c, const char *name, const char *fmt, ...);
void inginxClientAddReplyPrintf(inginxClient *c, const char *fmt, ...);
void inginxClientAddBodyPrintf(inginxClient *c, const char *fmt, ...);
#endif

void inginxClientClose(inginxClient *c);

#ifdef __cplusplus
}
#endif

#endif /* __INGINX_H__ */
