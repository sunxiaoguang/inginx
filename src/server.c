#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "adlist.h"
#include "server.h"
#include "zmalloc.h"

#define runWithPeriod(_s_, _ms_) if ((_ms_ <= 1000 / _s_->hz) || !(_s_->cronloops%((_ms_)/(1000/_s_->hz))))

static inline void serverDispatchEvent(inginxServer *s, inginxClient *c, inginxEventType event, void *data)
{
  if ((s->listenerMask & event) == event) {
    s->listener(s, c, event, data, s->listenerData);
  }
}

static void doCreateServer(inginxServer *s)
{
  s->clients = listCreate();
  s->pending = listCreate();
  s->closing = listCreate();
  s->listening = listCreate();
  s->hz = 10;
  s->maxIdleTime = 1000000000;
  s->parser = http_parser_execute_strict;
}

inginxServer *inginxServerCreate()
{
  inginxServer *s = zcalloc(sizeof(inginxServer));
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  doCreateServer(s);
  return s;
}

inginxServer *inginxServerGroupCreate(int32_t size, int32_t useProcess)
{
  int32_t idx;
  if (size <= 1) {
    return inginxServerCreate();
  }
  inginxServer *s = zcalloc(sizeof(inginxServer) * (size + 1));
  inginxServer *worker;
  s->group = s + 1;
  s->groupSize = size;
  for (idx = 0; idx < size; ++idx) {
    worker = s->group + idx;
    doCreateServer(worker);
  }
  return s;
}

/* Return the UNIX time in microseconds */
static int64_t ustime(void) {
  struct timeval tv;
  int64_t ust;

  gettimeofday(&tv, NULL);
  ust = ((int64_t) tv.tv_sec) * 1000000;
  ust += tv.tv_usec;
  return ust;
}

/* Return the UNIX time in milliseconds */
static int64_t mstime(void) {
  return ustime() / 1000;
}

/* We take a cached value of the unix time in the global state because with
 * virtual memory and aging there is to store the current time in objects at
 * every object access, and accuracy is not needed. To access a global var is
 * a lot faster than calling time(NULL) */
static void updateCachedTime(inginxServer *s) {
  s->unixTime = time(NULL);
  s->msTime = mstime();
}

/* Check for timeouts. Returns non-zero if the client was terminated.
 * The function gets the current time in milliseconds as argument since
 * it gets called multiple times in a loop, so calling gettimeofday() for
 * each iteration would be costly without any actual gain. */
static int32_t clientsCronHandleTimeout(inginxServer *s, inginxClient *c, int64_t now_ms) {
  time_t now = now_ms / 1000;

  if (s->maxIdleTime && (now - c->lastInteraction > s->maxIdleTime)) {
    inginxClientFree(s->el, c);
    return 1;
  }
  return 0;
}

#define CLIENTS_CRON_MIN_ITERATIONS 5
static void clientsCron(inginxServer *s) {
  /* Make sure to process at least numclients/server.hz of clients
   * per call. Since this function is called server.hz times per second
   * we are sure that in the worst case we process all the clients in 1
   * second. */
  int32_t numclients = listLength(s->clients);
  int32_t iterations = numclients / s->hz;
  int64_t now = mstime();

  /* Process at least a few clients while we are at it, even if we need
   * to process less than CLIENTS_CRON_MIN_ITERATIONS to meet our contract
   * of processing each client once per second. */
  if (iterations < CLIENTS_CRON_MIN_ITERATIONS)
      iterations = (numclients < CLIENTS_CRON_MIN_ITERATIONS) ?
                   numclients : CLIENTS_CRON_MIN_ITERATIONS;

  while (listLength(s->clients) && iterations--) {
    inginxClient *c;
    listNode *head;

    /* Rotate the list, take the current head, process.
     * This way if the client must be removed from the list it's the
     * first element and we don't incur into O(N) computation. */
    listRotate(s->clients);
    head = listFirst(s->clients);
    c = listNodeValue(head);
    /* The following functions do different service checks on the client.
     * The protocol is that they return non-zero if the client was
     * terminated. */
    if (clientsCronHandleTimeout(s, c,now)) continue;
  }
}

static int32_t serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
  AE_NOTUSED(id);
  AE_NOTUSED(clientData);
  inginxServer *s = eventLoop->data;

  /* Update the time cache. */
  updateCachedTime(s);

  /* We need to do a few operations on clients asynchronously. */
  clientsCron(s);

  /* Close clients that need to be closed asynchronous */
  inginxClientsFreeInAsyncFreeQueue(eventLoop);

  s->cronLoops++;

  if (s->shutdown) {
    aeStop(eventLoop);
  }

  return 1000 / s->hz;
}

static inline void doServerBind(inginxServer *server, char *address, int32_t port, 
    int32_t backlog, int32_t reusePort)
{
  int32_t fd6 = anetTcp6Server(server->error, port, address, backlog, reusePort);
  int32_t fd4 = anetTcpServer(server->error, port, address, backlog, reusePort);
  if (fd6 == ANET_ERR && fd4 == ANET_ERR) {
    INGINX_LOG_ERROR(server, "Could not create listening socket %s. %s", address, server->error);
    return;
  }
  if (fd6 != ANET_ERR) {
    anetNonBlock(server->error, fd6);
    listAddNodeTail(server->listening, (void *) (intptr_t) fd6);
  }
  if (fd4 != ANET_ERR) {
    anetNonBlock(server->error, fd4);
    listAddNodeTail(server->listening, (void *) (intptr_t) fd4);
  }
}

inginxServer *inginxServerHz(inginxServer *server, int32_t hz)
{
  if (server != NULL) {
    server->hz = hz;
  }
  return server;
}
inginxServer *inginxServerBind(inginxServer *s, const char *address, int32_t backlog)
{
  char *pos;
  char *bindaddr;
  int32_t idx = 0, port, length;
  if (s == NULL) {
    return s;
  }
  if ((pos = strchr(address, ':')) == NULL) {
    bindaddr = (char *) address;
    port = 80;
  } else {
    length = pos - address;
    bindaddr = zmalloc(length + 1);
    memcpy(bindaddr, address, length); 
    bindaddr[length] = '\0';
    port = strtol(pos + 1, NULL, 10);
  }

  if (s->group) {
    for (idx = 0; idx < s->groupSize; ++idx) {
      doServerBind(s->group + idx, bindaddr, port, backlog, 1);
    }
  } else {
    doServerBind(s, bindaddr, port, backlog, 0);
  }

  if (bindaddr != address) {
    zfree(bindaddr);
  }
  return s;
}

static void doServerConnectionLimit(inginxServer *server, int32_t limit)
{
  if (server->el) {
    if (aeResizeSetSize(server->el, limit) == AE_ERR) {
      INGINX_LOG_ERROR(server, "Could not set connection limit to %d", limit);
    }
  } else {
    server->el = aeCreateEventLoop(limit);
    server->el->data = server;
  }
  server->events = zrealloc(server->events, limit * sizeof(inginxFileEvent));
}

inginxServer *inginxServerConnectionLimit(inginxServer *s, int32_t limit)
{
  int32_t idx;
  if (s == NULL) {
    return s;
  }
  if (s->group) {
    for (idx = 0; idx < s->groupSize; ++idx) {
      doServerConnectionLimit(s->group + idx, limit);
    }
  } else {
    doServerConnectionLimit(s, limit);
  }
  return s;
}

static inginxClient *createClient(inginxServer *s, int32_t fd) {
  inginxClient *c = zcalloc(sizeof(inginxClient));

  /* passing -1 as fd it is possible to create a non connected client.
   * This is useful since all the commands needs to be executed
   * in the context of a client. When commands are executed in other
   * contexts (for instance a Lua script) we need a non connected client. */
  if (fd != -1) {
    anetNonBlock(s->error, fd);
    anetEnableTcpNoDelay(s->error, fd);
    anetKeepAlive(s->error, fd, 1);
    if (aeCreateFileEvent(s->el, fd, AE_READABLE, inginxClientReadFrom, c) == AE_ERR) {
      close(fd);
      zfree(c);
      return NULL;
    }
    listAddNodeTail(s->clients, c);
  }
  c->server = s;

  http_parser_init(&c->parser, HTTP_REQUEST);
  c->message.major = c->message.minor = c->parser.http_major = c->parser.http_minor = 1;
  c->parser.data = c;
  c->id = 0;
  c->fd = fd;
  c->message.headers = listCreate();
  c->reply = listCreate();
  c->lastInteraction = s->unixTime;

  serverDispatchEvent(s, c, INGINX_EVENT_TYPE_CONNECTED, c);
  return c;
}

static void acceptTcpHandler(aeEventLoop *el, int32_t fd, void *privdata, int32_t mask) {
  int32_t cport, cfd, max = 16;
  char cip[256];
  inginxServer *s = privdata;

  while (max--) {
    cfd = anetTcpAccept(s->error, fd, cip, sizeof(cip), &cport);
    if (cfd == ANET_ERR) {
      if (errno != EWOULDBLOCK) {
        strerror_r(errno, s->error, sizeof(s->error));
        INGINX_LOG_ERROR(el->data, "Could not accept new connection from client. %s", s->error);
      }
      return;
    } else {
      createClient(s, cfd);
    }
  }
}

/* This function gets called every time Redis is entering the
 * main loop of the event driven library, that is, before to sleep
 * for ready file descriptors. */
static void beforeSleep(struct aeEventLoop *eventLoop) {
  /* Handle writes with pending output buffers. */
  inginxClientsHandleWithPendingWrites(eventLoop);
}

static inline void doServerMain(inginxServer *server)
{
  listIter *it = listGetIterator(server->listening, AL_START_HEAD);
  listNode *ln;
  int32_t succeeded = 0;
  while ((ln = listNext(it)) != NULL) {
    if (aeCreateFileEvent(server->el, (int32_t) (intptr_t) ln->value, 
        AE_READABLE, acceptTcpHandler, server) == AE_OK) {
      ++succeeded;
    }
  }
  if (succeeded == 0) {
    INGINX_LOG_ERROR(server, "Could not create file event for any of the listening socket");
    goto cleanupExit;
  }
  if (aeCreateTimeEvent(server->el, 1, serverCron, NULL, NULL) == AE_ERR) {
    INGINX_LOG_ERROR(server, "Could not create timmer for server cron task");
    goto cleanupExit;
  }
  aeSetBeforeSleepProc(server->el, beforeSleep);
  aeMain(server->el);
  listRewind(server->listening, it);
  while ((ln = listNext(it)) != NULL) {
    aeDeleteFileEvent(server->el, (int32_t) (intptr_t) ln->value, AE_READABLE);
  }

cleanupExit:
  listReleaseIterator(it);
}

inginxServer* inginxServerMain(inginxServer *s)
{
  int32_t idx;
  pthread_t *threads;
  void *rc;
  if (s == NULL) {
    return s;
  }
  signal(SIGPIPE, SIG_IGN);
  if (s->group) {
    threads = zmalloc(sizeof(pthread_t) * s->groupSize);
    for (idx = 0; idx < s->groupSize; ++idx) {
      pthread_create(threads + idx, NULL, (void *(*)(void *)) doServerMain, s->group + idx);
    }
    for (idx = 0; idx < s->groupSize; ++idx) {
      pthread_join(threads[idx], &rc);
    }
    zfree(threads);
  } else {
    doServerMain(s);
  }
  return s;
}

static inline void doServerShutdown(inginxServer *s)
{
  s->shutdown = 1;
  __sync_synchronize();
}

inginxServer *inginxServerShutdown(inginxServer *s)
{
  int32_t idx;
  if (s == NULL) {
    return s;
  }
  if (s->group) {
    for (idx = 0; idx < s->groupSize; ++idx) {
      doServerShutdown(s->group + idx);
    }
  } else {
    doServerShutdown(s);
  }
  return s;
}

inline static void doServerFree(inginxServer *server)
{
  listIter *it = listGetIterator(server->listening, AL_START_HEAD);
  listNode *ln;
  while ((ln = listNext(it)) != NULL) {
    close((int32_t) (intptr_t) ln->value);
  }
  listReleaseIterator(it);
  if (server->el) {
    aeDeleteEventLoop(server->el);
  }
  if (server->clients) {
    listRelease(server->clients);
  }
  if (server->pending) {
    listRelease(server->pending);
  }
  if (server->closing) {
    listRelease(server->closing);
  }
  if (server->listening) {
    listRelease(server->listening);
  }
  if (server->events) {
    zfree(server->events);
  }
}

void inginxServerFree(inginxServer *s)
{
  int32_t idx;
  if (s == NULL) {
    return; 
  }
  if (s->group) {
    for (idx = 0; idx < s->groupSize; ++idx) {
      doServerFree(s->group + idx);
    }
  } else {
    doServerFree(s);
  }
  zfree(s);
}

static inline void doServerLogger(inginxServer *s, inginxLogger logger, inginxLogLevel level, void *data)
{
  s->logger = logger;
  s->loggerLevel = level;
  s->loggerData = data;
}

inginxServer *inginxServerLogger(inginxServer *s, inginxLogger logger, inginxLogLevel level, void *data)
{
  int32_t idx;
  if (s == NULL) {
    return s;
  }
  if (s->group) {
    for (idx = 0; idx < s->groupSize; ++idx) {
      doServerLogger(s->group + idx, logger, level, data);
    }
  } else {
    doServerLogger(s, logger, level, data);
  }
  return s;
}

static inline void doServerListener(inginxServer *s, inginxListener listener, int32_t mask, void *opaque)
{
  s->listener = listener;
  s->listenerMask = mask;
  s->listenerData = opaque;
}

inginxServer *inginxServerListener(inginxServer *s, inginxListener listener, int32_t mask, void *opaque)
{
  int32_t idx;
  if (s == NULL) {
    return s;
  }
  if (s->group) {
    for (idx = 0; idx < s->groupSize; ++idx) {
      doServerListener(s->group + idx, listener, mask, opaque);
    }
  } else {
    doServerListener(s, listener, mask, opaque);
  }
  return s;
}

#define MAX_LOG_LINE_SIZE   (4096)

void inginxServerLog(inginxServer *s, inginxLogLevel level, const char *func, const char *file, uint32_t line, const char *fmt, ...)
{
  va_list args;
  char buffer[MAX_LOG_LINE_SIZE];
  int32_t bufferSize = sizeof(buffer);
  int32_t size;

  if (s->logger == NULL) {
    return;
  }

  va_start(args, fmt);
  size = vsnprintf(buffer, bufferSize, fmt, args);
  va_end(args);

  if (size == 0) {
    buffer[0] = '\0';
  }

  s->logger(s, level, func, file, line, buffer, s->loggerData);
}

void inginxServerSimpleLogger(inginxServer *s, inginxLogLevel level, const char *func, const char *file, uint32_t line, const char *log, void *opaque)
{
  char buffer[MAX_LOG_LINE_SIZE];
  int32_t bufferSize = sizeof(buffer);
  static char levels[] = {'T', 'D', 'I', 'W', 'E', 'F', 'A'};
  time_t now = s->unixTime;
  char *rest = ctime_r(&now, buffer);
  size_t len = strlen(rest) - 1;

  snprintf(rest + len, bufferSize - len, " [%c|%s|%s:%u] %s", levels[level], func, file, line, log);
  printf("%s\n", buffer);
}

void inginxServerClientRequest(inginxServer *server, inginxClient *client)
{
  serverDispatchEvent(server, client, INGINX_EVENT_TYPE_REQUEST, &client->message);
}

void inginxServerClientDisconnected(inginxServer *server, inginxClient *client)
{
  serverDispatchEvent(server, client, INGINX_EVENT_TYPE_DISCONNECTED, client);
}

void inginxServerClientDestroyed(inginxServer *server, inginxClient *client)
{
  serverDispatchEvent(server, client, INGINX_EVENT_TYPE_DESTROYED, client);
}

const char *inginxVersion(void)
{
  return "1.0";
}

int32_t inginxServerIsDispatchingThread(inginxServer *server)
{
  return pthread_equal(server->dispatchingThread, pthread_self());
}

static void doInginxServerParser(inginxServer *server, http_parser_execute parser)
{
  int32_t idx;
  server->parser = parser;
  if (server->group) {
    for (idx = 0; idx < server->groupSize; ++idx) {
      server->group[idx].parser = parser;
    }
  }
}

inginxServer *inginxServerStrict(inginxServer *server)
{
  if (server != NULL) {
    doInginxServerParser(server, http_parser_execute_strict);
  }
  return server;
}

inginxServer *inginxServerRelaxed(inginxServer *server)
{
  if (server != NULL) {
    doInginxServerParser(server, http_parser_execute_relaxed);
  }
  return server;
}

static void inginxServerFileEvent(aeEventLoop *el, int32_t fd, void *opaque, int32_t mask)
{
  inginxFileEvent *event = opaque;
  int32_t rfired = 0;
  if ((event->mask & mask) & INGINX_FILE_EVENT_READABLE) {
    rfired = 1;
    event->read(el->data, fd, mask, event->opaque);
  }
  if ((event->mask & mask) & INGINX_FILE_EVENT_WRITABLE) {
    if (!rfired || event->write != event->read) {
      event->write(el->data, fd, mask, event->opaque);
    }
  }
}

int32_t inginxServerCreateFileEvent(inginxServer *server, int32_t fd, int32_t mask, inginxFileEventListener listener, void *opaque)
{
  inginxFileEvent *event = server->events + fd;
  int32_t rc = aeCreateFileEvent(server->el, fd, mask, inginxServerFileEvent, event);
  if (rc == AE_OK) {
    event->mask = aeGetFileEvents(server->el, fd);
    if (mask & INGINX_FILE_EVENT_READABLE) {
      event->read = listener;
    }
    if (mask & INGINX_FILE_EVENT_WRITABLE) {
      event->write = listener;
    }
  }
  return rc;
}

int32_t inginxServerDeleteFileEvent(inginxServer *server, int32_t fd, int32_t mask)
{
  if (fd < 0 || aeGetSetSize(server->el) <= fd) {
    return  0;
  }
  aeDeleteFileEvent(server->el, fd, mask);
  server->events[fd].mask = aeGetFileEvents(server->el, fd);
  return 0;
}

int32_t inginxServerGetFileEvents(inginxServer *server, int32_t fd)
{
  return aeGetFileEvents(server->el, fd);
}

int32_t inginxServerConnect(inginxServer *server, const char *addr, uint16_t port)
{
  return anetTcpNonBlockConnect(server->error, (char *) addr, port);
}
