#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "server.h"
#include "zmalloc.h"

static int onMessageBegin(http_parser *parser);
static int onUrl(http_parser*, const char *at, size_t length);
static int onStatus(http_parser*, const char *at, size_t length);
static int onHeaderField(http_parser*, const char *at, size_t length);
static int onHeaderValue(http_parser*, const char *at, size_t length);
static int onHeadersComplete(http_parser *parser);
static int onBody(http_parser*, const char *at, size_t length);
static int onMessageComplete(http_parser *parser);
static int onChunkHeader(http_parser *parser);
static int onChunkComplete(http_parser *parser);
static void resetMessage(inginxMessage *message);

static http_parser_settings settings = {
  onMessageBegin,
  onUrl,
  onStatus,
  onHeaderField,
  onHeaderValue,
  onHeadersComplete,
  onBody,
  onMessageComplete,
  onChunkHeader,
  onChunkComplete,
};

static const char *httpCodeDesc(int32_t code)
{
  switch (code) {
    case 100:
      return "Continue";
    case 101:
      return "Switching Protocols";
    case 102:
      return "Processing";
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 202:
      return "Accepted";
    case 203:
      return "Non-Authoritative Information";
    case 204:
      return "No Content";
    case 205:
      return "Reset Content";
    case 206:
      return "Partial Content";
    case 207:
      return "Multi-Status";
    case 208:
      return "Already Reported";
    case 226:
      return "IM Use";
    case 300:
      return "Multiple Choices";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 303:
      return "See Other";
    case 304:
      return "Not Modified";
    case 305:
      return "Use Proxy";
    case 306:
      return "Switch Proxy";
    case 307:
      return "Temporary Redirect";
    case 308:
      return "Permanent Redirect";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 402:
      return "Payment Required";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 406:
      return "Not Acceptable";
    case 407:
      return "Proxy Authentication Required";
    case 408:
      return "Request Timeout";
    case 409:
      return "Conflict";
    case 410:
      return "Gone";
    case 411:
      return "Length Required";
    case 412:
      return "Precondition Failed";
    case 413:
      return "Payload Too Large";
    case 414:
      return "URI Too Long";
    case 415:
      return "Unsupported Media Type";
    case 416:
      return "Range Not Satisfiable";
    case 417:
      return "Expectation Failed";
    case 418:
      return "I'm a teapot";
    case 421:
      return "Misdirected Request";
    case 422:
      return "Unprocessable Entity";
    case 423:
      return "Locked";
    case 424:
      return "Failed Dependency";
    case 426:
      return "Upgrade Required";
    case 428:
      return "Precondition Required";
    case 429:
      return "Too Many Requests";
    case 431:
      return "Request Header Fields Too Large";
    case 451:
      return "Unavailable For Legal Reasons";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    case 504:
      return "Gateway Timeout";
    case 505:
      return "HTTP Version Not Supported";
    case 506:
      return "Variant Also Negotiates";
    case 507:
      return "Insufficient Storage";
    case 508:
      return "Loop Detected";
    case 510:
      return "Not Extended";
    case 511:
      return "Network Authentication Required";
    default:
      return "";
  }
}

static void inginxClientFreeAsync(inginxClient *c);

static int clientHasPendingReplies(inginxClient *c) {
    return c->position || listLength(c->reply);
}

/* Write data in output buffers to client. Return C_OK if the client
 * is still valid after the call, C_ERR if it was freed. */
static int writeToClient(aeEventLoop *el, int fd, inginxClient *c, int handler_installed) {
  ssize_t nwritten = 0, totwritten = 0;
  size_t objlen;
  sds reply;
  inginxServer *s = el->data;

  while (clientHasPendingReplies(c)) {
    if (c->position > 0) {
      nwritten = write(fd, c->buffer + c->sent, c->position - c->sent);
      if (nwritten <= 0) break;
      c->sent += nwritten;
      totwritten += nwritten;

      /* If the buffer was sent, set position to zero to continue with
       * the remainder of the reply. */
      if (c->sent == c->position) {
        c->position = 0;
        c->sent = 0;
      }
    } else {
      reply = listNodeValue(listFirst(c->reply));
      objlen = sdslen(reply);

      if (objlen == 0) {
        listDelNode(c->reply, listFirst(c->reply));
        c->replyBytes -= objlen;
        sdsfree(reply);
        continue;
      }

      nwritten = write(fd, reply + c->sent, objlen - c->sent);
      if (nwritten <= 0) break;
      c->sent += nwritten;
      totwritten += nwritten;

      /* If we fully sent the object on head go to the next one */
      if (c->sent == objlen) {
        listDelNode(c->reply, listFirst(c->reply));
        c->sent = 0;
        c->replyBytes -= objlen;
        sdsfree(reply);
      }
    }
    /* Note that we avoid to send more than NET_MAX_WRITES_PER_EVENT
     * bytes, in a single threaded server it's a good idea to serve
     * other clients as well, even if a very large request comes from
     * super fast link that is always able to accept data (in real world
     * scenario think about 'KEYS *' against the loopback interface).
     *
     * However if we are over the maxmemory limit we ignore that and
     * just deliver as much data as it is possible to deliver. */
    /*
       server.stat_net_output_bytes += totwritten;
       if (totwritten > NET_MAX_WRITES_PER_EVENT &&
       (server.maxmemory == 0 ||
       zmalloc_used_memory() < server.maxmemory)) break;
       */
  }
  if (nwritten == -1) {
    if (errno == EAGAIN) {
      nwritten = 0;
    } else {
      strerror_r(errno, s->error, sizeof(s->error));
      INGINX_LOG_TRACE(el->data, "Error writing to client: %s", s->error);
      inginxClientFree(el, c);
      return C_ERR;
    }
  }
  if (totwritten > 0) {
    /* For clients representing masters we don't count sending data
     * as an interaction, since we always send REPLCONF ACK commands
     * that take some time to just fill the socket output buffer.
     * We just rely on data / pings received for timeout detection. */
    c->lastInteraction = s->unixTime;
  }
  if (!clientHasPendingReplies(c)) {
    c->sent = 0;
    if (handler_installed) aeDeleteFileEvent(el, c->fd, AE_WRITABLE);

    /* Close connection after entire reply has been sent. */
    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) {
      inginxClientFree(el, c);
      return C_ERR;
    }
  }
  return C_OK;
}

/* Write event handler. Just send data to the client. */
static void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    writeToClient(el, fd, privdata, 1);
}

/* This function is called just before entering the event loop, in the hope
 * we can just write the replies to the client output buffer without any
 * need to use a syscall in order to install the writable event handler,
 * get it called, and so forth. */
int inginxClientsHandleWithPendingWrites(aeEventLoop *el) {
    inginxServer *s = el->data;
    listIter li;
    listNode *ln;
    int processed = listLength(s->pending);
  
    listRewind(s->pending, &li);
    while ((ln = listNext(&li))) {
        inginxClient *c = listNodeValue(ln);
        c->flags &= ~CLIENT_PENDING_WRITE;
        listDelNode(s->pending, ln);
  
        /* Try to write buffers to the client socket. */
        if (writeToClient(el, c->fd, c, 0) == C_ERR) continue;
  
        /* If there is nothing left, do nothing. Otherwise install
         * the write handler. */
        if (clientHasPendingReplies(c) &&
            aeCreateFileEvent(el, c->fd, AE_WRITABLE, sendReplyToClient, c) == AE_ERR)
        {
            inginxClientFreeAsync(c);
        }
    }
    return processed;
}

/* Schedule a client to free it at a safe time in the serverCron() function.
 * This function is useful when we need to terminate a client but we are in
 * a context where calling freeClient() is not possible, because the client
 * should be valid for the continuation of the flow of the program. */
void inginxClientFreeAsync(inginxClient *c) {
    if (c->flags & CLIENT_CLOSE_ASAP) return;
    c->flags |= CLIENT_CLOSE_ASAP;
    listAddNodeTail(c->server->closing, c);
}

void inginxClientsFreeInAsyncFreeQueue(aeEventLoop *el) {
  inginxServer *s = el->data;
  while (listLength(s->closing)) {
    listNode *ln = listFirst(s->closing);
    inginxClient *c = listNodeValue(ln);
    c->flags &= ~CLIENT_CLOSE_ASAP;
    inginxClientFree(el, c);
    listDelNode(s->closing,ln);
  }
}

/* Remove the specified client from global lists where the client could
 * be referenced, not including the Pub/Sub channels.
 * This is used by inginxClientFree() and replicationCacheMaster(). */
static void unlinkClient(aeEventLoop *el, inginxClient *c) {
    listNode *ln;
    inginxServer *s = el->data;

    /* If this is marked as current client unset it. */
    if (s->current == c) s->current = NULL;

    /* Certain operations must be done only if the client has an active socket.
     * If the client was already unlinked or if it's a "fake client" the
     * fd is already set to -1. */
    if (c->fd != -1) {
        /* Remove from the list of active clients. */
        ln = listSearchKey(s->clients, c);
        assert(ln != NULL);
        listDelNode(s->clients, ln);

        /* Unregister async I/O handlers and close the socket. */
        aeDeleteFileEvent(el, c->fd, AE_READABLE);
        aeDeleteFileEvent(el, c->fd, AE_WRITABLE);
        close(c->fd);
        c->fd = -1;
        inginxServerClientDisconnected(s, c);
    }

    /* Remove from the list of pending writes if needed. */
    if (c->flags & CLIENT_PENDING_WRITE) {
        ln = listSearchKey(s->pending, c);
        assert(ln != NULL);
        listDelNode(s->pending, ln);
        c->flags &= ~CLIENT_PENDING_WRITE;
    }
}

void inginxClientFree(aeEventLoop *el, inginxClient *c) {
    listNode *ln;
    inginxServer *s = el->data;
    /* Free data structures. */
    listRelease(c->reply);
    resetMessage(&c->message);
    if (c->message.headers) {
      listRelease(c->message.headers);
    }

    /* Unlink the client: this will close the socket, remove the I/O
     * handlers, and remove references of the client from different
     * places where active clients may be referenced. */
    unlinkClient(el, c);

    /* If this client was scheduled for async freeing we need to remove it
     * from the queue. */
    if (c->flags & CLIENT_CLOSE_ASAP) {
        ln = listSearchKey(s->closing, c);
        assert(ln != NULL);
        listDelNode(s->closing, ln);
    }

    zfree(c);
}

/* This function is called every time we are going to transmit new data
 * to the client. The behavior is the following:
 *
 * If the client should receive new data (normal clients will) the function
 * returns C_OK, and make sure to install the write handler in our event
 * loop so that when the socket is writable new data gets written.
 *
 * If the client should not receive new data, because it is a fake client
 * (used to load AOF in memory), a master or because the setup of the write
 * handler failed, the function returns C_ERR.
 *
 * The function may return C_OK without actually installing the write
 * event handler in the following cases:
 *
 * 1) The event handler should already be installed since the output buffer
 *    already contained something.
 * 2) The client is a slave but not yet online, so we want to just accumulate
 *    writes in the buffer but not actually sending them yet.
 *
 * Typically gets called every time a reply is built, before adding more
 * data to the clients output buffers. If the function returns C_ERR no
 * data should be appended to the output buffers. */
static int prepareClientToWrite(inginxClient *c) {
    /* CLIENT REPLY OFF / SKIP handling: don't send replies. */
    if (c->flags & (CLIENT_REPLY_OFF|CLIENT_REPLY_SKIP)) return C_ERR;

    /* Schedule the client to write the output buffers to the socket only
     * if not already done (there were no pending writes already and the client
     * was yet not flagged), and, for slaves, if the slave can actually
     * receive writes at this stage. */
    if (!clientHasPendingReplies(c) && !(c->flags & CLIENT_PENDING_WRITE))
    {
        /* Here instead of installing the write handler, we just flag the
         * client and put it into a list of clients that have something
         * to write to the socket. This way before re-entering the event
         * loop, we can try to directly write to the client sockets avoiding
         * a system call. We'll only really install the write handler if
         * we'll not be able to write the whole reply at once. */
        c->flags |= CLIENT_PENDING_WRITE;
        listAddNodeHead(c->server->pending, c);
    }

    /* Authorize the caller to queue in the output buffer of this client. */
    return C_OK;
}

static int addReplyToBuffer(inginxClient *c, sds msg) {
  size_t len = sdslen(msg);
  size_t available = sizeof(c->buffer) - c->position;

  if (c->flags & CLIENT_CLOSE_AFTER_REPLY) {
    goto cleanupExit;
  }

  /* If there already are entries in the reply list, we cannot
   * add anything more to the static buffer. */
  if (listLength(c->reply) > 0) return C_ERR;

  /* Check that the buffer has enough space available for this string. */
  if (len > available) return C_ERR;

  memcpy(c->buffer + c->position, msg, len);
  c->position += len;

cleanupExit:
  sdsfree(msg);
  return C_OK;
}

/* The function checks if the client reached output buffer soft or hard
 * limit, and also update the state needed to check the soft limit as
 * a side effect.
 *
 * Return value: non-zero if the client reached the soft or the hard limit.
 *               Otherwise zero is returned. */
static int checkClientOutputBufferLimits(inginxClient *c) {
#if 0
    int soft = 0, hard = 0, class;
    unsigned long used_mem = getClientOutputBufferMemoryUsage(c);

    class = getClientType(c);
    /* For the purpose of output buffer limiting, masters are handled
     * like normal clients. */
    if (class == CLIENT_TYPE_MASTER) class = CLIENT_TYPE_NORMAL;

    if (server.client_obuf_limits[class].hard_limit_bytes &&
        used_mem >= server.client_obuf_limits[class].hard_limit_bytes)
        hard = 1;
    if (server.client_obuf_limits[class].soft_limit_bytes &&
        used_mem >= server.client_obuf_limits[class].soft_limit_bytes)
        soft = 1;

    /* We need to check if the soft limit is reached continuously for the
     * specified amount of seconds. */
    if (soft) {
        if (c->obuf_soft_limit_reached_time == 0) {
            c->obuf_soft_limit_reached_time = server.unixtime;
            soft = 0; /* First time we see the soft limit reached */
        } else {
            time_t elapsed = server.unixtime - c->obuf_soft_limit_reached_time;

            if (elapsed <=
                server.client_obuf_limits[class].soft_limit_seconds) {
                soft = 0; /* The client still did not reached the max number of
                             seconds for the soft limit to be considered
                             reached. */
            }
        }
    } else {
        c->obuf_soft_limit_reached_time = 0;
    }
    return soft || hard;
#endif
    return 0;
}

/* Asynchronously close a client if soft or hard limit is reached on the
 * output buffer size. The caller can check if the client will be closed
 * checking if the client CLIENT_CLOSE_ASAP flag is set.
 *
 * Note: we need to close the client asynchronously because this function is
 * called from contexts where the client can't be freed safely, i.e. from the
 * lower level functions pushing data inside the client output buffers. */
static void asyncCloseClientOnOutputBufferLimitReached(inginxClient *c) {
    assert(c->replyBytes < INT32_MAX - (1024*64));
    if (c->replyBytes == 0 || c->flags & CLIENT_CLOSE_ASAP) return;
    if (checkClientOutputBufferLimits(c)) {
        inginxClientFreeAsync(c);
    }
}

static void addReplyToList(inginxClient *c, sds msg) {
  size_t len;
  if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return;

  len = sdslen(msg);
  if (listLength(c->reply) == 0) {
    listAddNodeTail(c->reply, msg);
    c->replyBytes += len;
  } else {
    listAddNodeTail(c->reply, msg);
    c->replyBytes += len;
  }
  asyncCloseClientOnOutputBufferLimitReached(c);
}

static void addReply(inginxClient *c, sds msg)
{
  if (prepareClientToWrite(c) != C_OK) return;

  /* This is an important place where we can avoid copy-on-write
   * when there is a saving child running, avoiding touching the
   * refcount field of the object if it's not needed.
   *
   * If the encoding is RAW and there is room in the static buffer
   * we'll be able to send the object to the client without
   * messing with its page. */
  if (addReplyToBuffer(c, msg) != C_OK) addReplyToList(c, msg);
}

void inginxClientReadFrom(aeEventLoop *el, int fd, void *privdata, int mask)
{
  inginxClient *c = privdata;
  char buffer[8 * 1024];
  ssize_t parsed;
  inginxServer *s = el->data;
  ssize_t nread = read(c->fd, buffer, sizeof(buffer));
  if (nread < 0) {
    strerror_r(errno, s->error, sizeof(s->error));
    INGINX_LOG_DEBUG(s, "Could not read from fd %zd. %s", nread, s->error);
    inginxClientFree(el, c);
    return;
  } else if (nread == 0) {
    INGINX_LOG_DEBUG(s, "Client closed connection");
    inginxClientFree(el, c);
    return;
  }
  parsed = s->parser(&c->parser, &settings, buffer, nread);
  if (c->parser.upgrade) {
    INGINX_LOG_WARN(s, "HTTP upgrade is not supported");
    inginxClientSendError(c, 500);
    return;
  }
  if (parsed != nread) {
    INGINX_LOG_WARN(s, "Invalid protocol when trying to parse request");
    inginxClientSendError(c, 400);
    inginxClientClose(c);
    return;
  }
}

static int onMessageBegin(http_parser *parser)
{
  inginxClient *c = parser->data;
  c->state = INGINX_CLIENT_STATE_BEGIN;
  return 0;
}

static int onUrl(http_parser *parser, const char *at, size_t length)
{
  inginxClient *c = parser->data;
  if (c->message.url == NULL) {
    c->message.url = sdsnewlen(at, length);
  } else {
    c->message.url = sdscatlen(c->message.url, at, length);
  }
  c->state = INGINX_CLIENT_STATE_URL;
  return 0;
}

static int onStatus(http_parser *parser, const char *at, size_t length)
{
  inginxClient *c = parser->data;
  c->state = INGINX_CLIENT_STATE_STATUS;
  return 0;
}

static int onHeaderField(http_parser *parser, const char *at, size_t length)
{
  inginxClient *c = parser->data;
  if (c->field == NULL) {
    c->field = sdsnewlen(at, length);
    if (c->value != NULL) {
      listAddNodeTail(c->message.headers, c->value);
      c->value = NULL;
    }
  } else {
    c->field = sdscatlen(c->field, at, length);
  }
  c->state = INGINX_CLIENT_STATE_HEADER_FIELD;
  return 0;
}

static int onHeaderValue(http_parser *parser, const char *at, size_t length)
{
  inginxClient *c = parser->data;
  if (c->value == NULL) {
    assert(c->field);
    listAddNodeTail(c->message.headers, c->field);
    c->field = NULL;
    c->value = sdsnewlen(at, length);
  } else {
    c->value = sdscatlen(c->value, at, length);
  }
  c->state = INGINX_CLIENT_STATE_HEADER_VALUE;
  return 0;
}

static int onHeadersComplete(http_parser *parser)
{
  inginxClient *c = parser->data;
  if (c->value != NULL) {
    listAddNodeTail(c->message.headers, c->value);
    c->value = NULL;
  }
  c->state = INGINX_CLIENT_STATE_HEADER_COMPLETE;
  return 0;
}

static int onBody(http_parser *parser, const char *at, size_t length)
{
  inginxClient *c = parser->data;
  if (c->message.body == NULL) {
    c->message.body = sdsnewlen(at, length);
  } else {
    c->message.body = sdscatlen(c->message.body, at, length);
  }
  c->state = INGINX_CLIENT_STATE_BODY;
  return 0;
}

static int onMessageComplete(http_parser *parser)
{
  inginxClient *c = parser->data;
  c->message.status = parser->status_code;
  c->message.method = parser->method;
  c->message.major = parser->http_major;
  c->message.minor = parser->http_minor;
  c->state = INGINX_CLIENT_STATE_COMPLETE;
  c->lengthSent = 0;
  inginxServerClientRequest(c->server, c);
  c->state = INGINX_CLIENT_STATE_BEGIN;
  resetMessage(&c->message);
  if (c->field) {
    sdsfree(c->field);
    c->field = NULL;
  }
  if (c->value) {
    sdsfree(c->value);
    c->value = NULL;
  }
  return 0;
}

static int onChunkHeader(http_parser *parser)
{
  return 0;
}

static int onChunkComplete(http_parser *parser)
{
  return 0;
}

static void resetMessage(inginxMessage *message)
{
  listIter *li;
  listNode *ln;
  if (message->urlDecoded != NULL) {
    if (message->urlDecoded != message->url) {
      sdsfree(message->urlDecoded);
    }
    message->urlDecoded = NULL;
  }
  if (message->url) {
    sdsfree(message->url);
    message->url = NULL;
  }
  if (message->parameter) {
    sdsfree(message->parameter);
    message->parameter = NULL;
  }
  li = listGetIterator(message->headers, AL_START_HEAD);
  while ((ln = listNext(li)) != NULL) {
    sdsfree(listNodeValue(ln));
    listDelNode(message->headers, ln);
  }
  listReleaseIterator(li);
  if (message->body) {
    sdsfree(message->body);
    message->body = NULL;
  }
  if (message->queryString) {
    message->queryString = NULL;
  }
  if (message->parameterCursor) {
    message->parameterCursor = NULL;
  }
}

void inginxClientClose(inginxClient *c)
{
  c->flags |= CLIENT_CLOSE_AFTER_REPLY;
}

void inginxClientSetStatus(inginxClient *c, int32_t status)
{
  addReply(c, sdscatprintf(sdsempty(), "HTTP/%u.%u %d %s\r\n", c->message.major, c->message.minor, status, httpCodeDesc(status)));
}

void inginxClientSendError(inginxClient *c, int32_t code)
{
  inginxClientSetStatus(c, code);
}

void inginxClientSendRedirect(inginxClient *c, const char *location)
{
  addReply(c, sdscatprintf(sdsempty(), "HTTP/1.1 302\r\nLocation: %s\r\nContent-Length: 0\r\n", location));
}

void inginxClientAddHeader(inginxClient *c, const char *name, const char *value)
{
  if (strcasecmp(name, "Content-Length") == 0) {
    c->lengthSent = 1;
  }
  addReply(c, sdscatprintf(sdsempty(), "%s: %s\r\n", name, value));
}

void inginxClientAddDateHeader(inginxClient *c, const char *name, int64_t date)
{
  struct tm tm;
  time_t time;
  char buffer[128];
  if (date <= 0) {
    time = c->server->msTime / 1000;
  } else {
    time = date / 1000000;
  }
  localtime_r(&time, &tm);
  strftime(buffer, sizeof(buffer), "%a, %d %b %G %T %Z", &tm);
  addReply(c, sdscatprintf(sdsempty(), "%s : %s\r\n", name, buffer));
}

void inginxClientAddReply(inginxClient *c, const char *body)
{
  inginxClientAddReplySize(c, body, strlen(body));
}

void inginxClientAddReplySize(inginxClient *c, const void *body, size_t size)
{
  addReply(c, sdsnewlen(body, size));
}

void inginxClientAddBody(inginxClient *c, const char *body)
{
  size_t size = body != NULL ? strlen(body) : 0;
  inginxClientAddBodySize(c, body, size);
}

void inginxClientAddBodySize(inginxClient *c, const void *body, size_t size)
{
  if (body != NULL) {
    if (!c->lengthSent) {
      addReply(c, sdscatprintf(sdsempty(), "Content-Length: %zd\r\n\r\n", size));
      c->lengthSent = 1;
    }
    addReply(c, sdsnewlen(body, size));
  } else {
    if (!c->lengthSent) {
      addReply(c, sdscatprintf(sdsempty(), "Content-Length: 0\r\n\r\n"));
      c->lengthSent = 1;
    }
  }
}

void inginxClientAddBodyVPrintf(inginxClient *c, const char *fmt, va_list args)
{
  sds body = sdscatvprintf(sdsempty(), fmt, args);
  if (!c->lengthSent) {
    addReply(c, sdscatprintf(sdsempty(), "Content-Length: %zd\r\n\r\n", sdslen(body)));
    c->lengthSent = 1;
  }
  addReply(c, body);
}

void inginxClientAddReplyVPrintf(inginxClient *c, const char *fmt, va_list args)
{
  addReply(c, sdscatvprintf(sdsempty(), fmt, args));
}

void inginxClientAddBodyPrintf(inginxClient *c, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  inginxClientAddBodyVPrintf(c, fmt, ap);
  va_end(ap);
}

void inginxClientAddReplyPrintf(inginxClient *c, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  inginxClientAddReplyVPrintf(c, fmt, ap);
  va_end(ap);
}

void inginxMessageVersion(const inginxMessage *message, uint16_t *major, uint16_t *minor)
{
  if (major != NULL) {
    *major = message->major;
  }
  if (minor != NULL) {
    *minor = message->minor;
  }
}

uint16_t inginxMessageStatus(const inginxMessage *message)
{
  return message->status;
}

inginxMethod inginxMessageMethod(const inginxMessage *message)
{
  return message->method;
}

const char* inginxMessageUrl(const inginxMessage *message)
{
  return message->url;
}

static void inginxMessageDecodeUrl(inginxMessage *message)
{
#define CHECK_DECODED                   \
  if (decoded == NULL) {                \
    decoded = sdsnewlen(src, idx);      \
  }
#define H2I(x) (isdigit(x) ? x - '0' : x - 'W')
  int32_t len = sdslen(message->url);
  const char *src = message->url;
  char chr, dst;
  int32_t idx, hi, lo;
  int32_t queryString = 0;
  sds decoded = NULL;
  for (idx = 0; idx < len; idx++) {
    switch ((chr = src[idx])) {
      case '%':
        if (idx < len - 2 && isxdigit((hi = src[idx + 1])) && isxdigit((lo = src[idx + 2]))) {
          hi = tolower(hi), lo = tolower(lo);
          CHECK_DECODED
          dst = (char) ((H2I(hi) << 4) | H2I(lo));
          idx += 2;
          decoded = sdscatlen(decoded, &dst, 1);
        } else {
          if (decoded != NULL) {
            sdsfree(decoded);
            decoded = NULL;
          }
          message->queryString = NULL;
          return;
        }
        break;
      case '+':
        if (queryString) {
          CHECK_DECODED
          dst = ' ';
          decoded = sdscatlen(decoded, &dst, 1);
          break;
        }
        goto appendDecoded;
      case '?':
        if (!queryString) {
          queryString = 1;
          message->queryString = (const char *)(decoded != NULL ? sdslen(decoded) : idx);
        }
        /* FALL THROUGH */
      default:
appendDecoded:
        if (decoded != NULL) {
          decoded = sdscatlen(decoded, src + idx, 1);
        }
    }
  }
  if (decoded == NULL) {
    message->urlDecoded = message->url;
  } else {
    message->urlDecoded = decoded;
  }
  if (queryString) {
    message->queryString = message->urlDecoded + (uintptr_t) (message->queryString) + 1;
  } else {
    message->queryString = NULL;
  }
#undef CHECK_DECODED
#undef H2I
  return;
}

const char *inginxMessageUrlDecoded(const inginxMessage *message)
{
  if (message->urlDecoded == NULL) {
    inginxMessageDecodeUrl((inginxMessage *) message);
  }
  return message->urlDecoded;
}

const char *inginxMessageHeader(const inginxMessage *message, const char *field)
{
  return inginxMessageHeaderNext(message, field, NULL);
}

const char *inginxMessageHeaderNext(const inginxMessage *message, const char *field, const char *cursor)
{
  listIter *li;
  listNode *ln, *ln2;
  sds value = NULL;
  if (message->headers == NULL) {
    return NULL;
  }
  li = listGetIterator(message->headers, AL_START_HEAD);
  while ((ln = listNext(li)) != NULL) {
    ln2 = listNext(li);
    if (strcasecmp(listNodeValue(ln), field) == 0) {
      value = listNodeValue(ln2);
      if (cursor != NULL) {
        if (value == cursor) {
          cursor = NULL;
        }
        value = NULL;
        continue;
      }
      break;
    }
  }
  listReleaseIterator(li);
  return value;
}

const char *inginxMessageBody(const inginxMessage *message)
{
  return message->body;
}

size_t inginxMessageBodyLength(const inginxMessage *message)
{
  return message->body != NULL ? sdslen(message->body) : 0;
}

void inginxClientAddHeaderVPrintf(inginxClient *c, const char *name, const char *fmt, va_list args)
{
  if (strcasecmp(name, "Content-Length") == 0) {
    c->lengthSent = 1;
  }
  addReply(c, sdscat(sdscatvprintf(sdscatprintf(sdsempty(), "%s: ", name), fmt, args), "\r\n"));
}

void inginxClientAddHeaderPrintf(inginxClient *c, const char *name, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  inginxClientAddHeaderVPrintf(c, name, fmt, ap);
  va_end(ap);
}

static int32_t inginxClientGetAddress(inginxClient *client, char *address, size_t size, uint16_t *port, int (*impl)(int, char *, size_t, int *))
{
  int tmp;
  if (impl(client->fd, address, size, &tmp) != 0) {
    return -1;
  }
  if (port) {
    *port = tmp;
  }
  return 0;
}

int32_t inginxClientGetRemoteAddress(inginxClient *client, char *address, size_t size, uint16_t *port)
{
  return inginxClientGetAddress(client, address, size, port, anetPeerToString);
}

int inginxClientGetLocalAddress(inginxClient *client, char *address, size_t size, uint16_t *port)
{
  return inginxClientGetAddress(client, address, size, port, anetSockName);
}

inginxClient *inginxClientConnect(inginxServer *server, const char *url, inginxMethod method)
{
  return NULL;
}

const char *inginxMessageParameter(const inginxMessage *message, const char *name)
{
  return inginxMessageParameterNext(message, name, NULL);
}

const char *inginxMessageParameterNext(const inginxMessage *constMessage, const char *name, const char *cursor)
{
  size_t nameLen;
  inginxMessage *message = (inginxMessage *) constMessage;
  const char *pos, *stop, *end;
  const char *url = inginxMessageUrlDecoded(message);
  if (url == NULL) {
    return NULL;
  }
  end = url + sdslen((char *) url);
  if (message->queryString == NULL || message->queryString + 1 == end) {
    return NULL;
  }

  if (cursor == NULL) {
    cursor = message->queryString;
  } else {
    cursor = message->parameterCursor;
  }

  nameLen = strlen(name);

  for (pos = cursor; pos + nameLen < end; pos++) {
    if ((pos == cursor || pos[-1] == '&') && pos[nameLen] == '=' && strncasecmp(name, pos, nameLen) == 0) {
      pos += nameLen + 1;
      stop = (const char *) memchr(pos, '&', (size_t)(end - pos));
      if (stop == NULL) {
        stop = end;
      }
      nameLen = stop - pos;
      if (message->parameter == NULL) {
        message->parameter = sdsnewlen(pos, nameLen);
      } else {
        message->parameter = sdscpylen(message->parameter, pos, nameLen);
      }
      message->parameterCursor = stop == end ? stop : stop + 1;
      return message->parameter;
    }
  }
  message->parameterCursor = NULL;
  return NULL;
}
