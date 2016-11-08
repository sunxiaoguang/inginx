/* Poll()-based ae.c module.
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <poll.h>
#include <string.h>

typedef struct aeApiState {
    struct pollfd *fds;
} aeApiState;

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    int fd = 0;
    aeApiState *state = eventLoop->apidata;

    state->fds = zrealloc(state->fds, sizeof(struct pollfd)*setsize);
    for (fd = 0; fd < setsize; ++fd) {
      state->fds[fd].fd = fd;
    }
    return 0;
}

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zcalloc(sizeof(aeApiState));

    if (!state) return -1;

    eventLoop->apidata = state;
    if (aeApiResize(eventLoop, eventLoop->setsize) == -1) {
      eventLoop->apidata = NULL;
      zfree(state);
      return -1;
    }
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    zfree(state->fds);
    zfree(eventLoop->apidata);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;

    if (mask & AE_READABLE) state->fds[fd].events |= POLLIN;
    if (mask & AE_WRITABLE) state->fds[fd].events |= POLLOUT;
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;

    if (mask & AE_READABLE) state->fds[fd].events &= ~POLLIN;
    if (mask & AE_WRITABLE) state->fds[fd].events &= ~POLLOUT;
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, j, numevents = 0;

    int timeout = (tvp->tv_sec * 1000) + (tvp->tv_usec / 1000);
    retval = poll(state->fds, eventLoop->maxfd + 1, timeout);
    if (retval > 0) {
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            aeFileEvent *fe = &eventLoop->events[j];

            if (fe->mask == AE_NONE) continue;
            if (fe->mask & AE_READABLE && state->fds[j].revents & POLLIN)
                mask |= AE_READABLE;
            if (fe->mask & AE_WRITABLE && state->fds[j].revents & POLLOUT)
                mask |= AE_WRITABLE;
            eventLoop->fired[numevents].fd = j;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

static char *aeApiName(void) {
    return "poll";
}
