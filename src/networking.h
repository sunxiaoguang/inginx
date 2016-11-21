#ifndef __INGNIX_NETWORKING__
#define __INGNIX_NETWORKING__

#include "http_parser.h"
#include "adlist.h"
#include "sds.h"
#include "ae.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define C_OK                    0
#define C_ERR                   -1

/* Client flags */
#define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
#define CLIENT_CLOSE_ASAP (1<<10)/* Close this client ASAP */
#define CLIENT_UNIX_SOCKET (1<<11) /* Client connected via Unix domain socket */
#define CLIENT_PENDING_WRITE (1<<21) /* Client has output to send but a write
                                        handler is yet not installed. */
#define CLIENT_REPLY_OFF (1<<22)   /* Don't send replies to client. */
#define CLIENT_REPLY_SKIP (1<<24)  /* Don't send just this reply. */

/* Protocol and I/O related defines */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define LONG_STR_SIZE      21          /* Bytes needed for long -> str + '\0' */

typedef enum inginxClientState {
  INGINX_CLIENT_STATE_BEGIN = 0,
  INGINX_CLIENT_STATE_URL = 1,
  INGINX_CLIENT_STATE_STATUS = 2,
  INGINX_CLIENT_STATE_HEADER_FIELD = 3,
  INGINX_CLIENT_STATE_HEADER_VALUE = 4,
  INGINX_CLIENT_STATE_HEADER_COMPLETE = 5,
  INGINX_CLIENT_STATE_BODY = 6,
  INGINX_CLIENT_STATE_CHUNK_HEADER = 7,
  INGINX_CLIENT_STATE_CHUNK_COMPLETE = 8,
  INGINX_CLIENT_STATE_COMPLETE = 9,
} inginxClientState;

typedef struct inginxMessage {
  uint16_t status;
  uint8_t method;
  uint8_t padding;
  uint16_t major;
  uint16_t minor;
  sds url;
  list *headers;
  sds body;
} inginxMessage;

typedef struct inginxClient {
  uint64_t id;
  int fd;
  char buffer[PROTO_IOBUF_LEN];
  size_t position;
  size_t sent;
  list *reply;
  int32_t replyBytes;
  int32_t flags;
  struct inginxServer *server;
  int64_t lastInteraction;
  inginxClientState state;
  uint8_t lengthSent;

  /* http related */
  http_parser parser;
  inginxMessage message;
  sds field;
  sds value;
} inginxClient;

void inginxClientReadFrom(aeEventLoop *el, int fd, void *privdata, int mask);
int inginxClientsHandleWithPendingWrites(aeEventLoop *el);
void inginxClientsFreeInAsyncFreeQueue(aeEventLoop *el);
void inginxClientFree(aeEventLoop *el, inginxClient *c);

inginxClient *inginxClientConnect(inginxServer *server, const char *url, inginxMethod method);

#ifdef __cplusplus
}
#endif

#endif /* __INGNIX_NETWORKING__ */
