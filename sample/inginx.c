#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <inginx.h>

void serverListener2(inginxServer *s, inginxClient *c, inginxEventType type, void *eventData, void *opaque);
void serverListener2(inginxServer *s, inginxClient *c, inginxEventType type, void *eventData, void *opaque)
{
  switch (type) {
    case INGINX_EVENT_TYPE_CONNECTED:
      printf("Connected\n");
      break;
    case INGINX_EVENT_TYPE_DISCONNECTED:
      printf("Disconnected\n");
      break;
    case INGINX_EVENT_TYPE_REQUEST:
      //printf("url is %s\n", inginxMessageUrl(eventData));
      //printf("ua is %s\n", inginxMessageHeader(eventData, "User-Agent"));
      //printf("host is %s\n", inginxMessageHeader(eventData, "Host"));
      inginxClientSetStatus(c, 200);
      inginxClientAddDateHeader(c, "Date", 0);
      inginxClientAddHeader(c, "Content-Type", "application/json");
      inginxClientAddHeader(c, "Connection", "keep-alive");
      inginxClientAddHeaderPrintf(c, "Thread", "%u", (unsigned int) (intptr_t) pthread_self());
      inginxClientAddHeader(c, "Server", "ingnix 1.0");
      inginxClientAddBody(c, "abcd");
      break;
    case INGINX_EVENT_TYPE_RESPONSE:
      printf("Response\n");
      break;
    case INGINX_EVENT_TYPE_ERROR:
      printf("Error\n");
      break;
    case INGINX_EVENT_TYPE_DESTROYED:
      printf("Client is destroyed\n");
      break;
    default:
      break;
  }
}

static inginxServer *volatile s;

static void sigShutdownHandler(int sig) {
  fprintf(stderr, "Shutdown server immediately\n");
  inginxServerShutdown(s);
}

void setupSignalHandlers(void);

void setupSignalHandlers(void) {
  struct sigaction act;
  /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
   * Otherwise, sa_handler is used. */
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  act.sa_handler = sigShutdownHandler;
  sigaction(SIGTERM, &act, NULL);
  sigaction(SIGINT, &act, NULL);
  return;
}



int main(int argc, const char **argv)
{
  setupSignalHandlers();
  s = inginxServerHz(inginxServerConnectionLimit(inginxServerBind(inginxServerListener(inginxServerLogger(inginxServerGroupCreate(3, 0),
            inginxServerSimpleLogger, INGINX_LOG_LEVEL_TRACE, NULL), serverListener2, 0xFFFFFFFF, NULL), "localhost:8888", 16), 1024), 1);
  inginxServerFree(inginxServerMain(s));
  fprintf(stderr, "Returned from server loop, free it now\n");
  return 0;
}

