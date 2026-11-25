#pragma once

#include <net/epoll_abort.h>
#include <net/socket.h>

struct EpollSocket {
    unsigned int events;
    SceNetEpollData data;
    std::weak_ptr<Socket> sock;
};

struct Epoll {
    std::map<int, EpollSocket> eventEntries;

    int add(int id, std::weak_ptr<Socket> sock, SceNetEpollEvent *ev);
    int del(int id);
    int mod(int id, SceNetEpollEvent *ev);
    int wait(SceNetEpollEvent *events, int maxevents, int timeout);

    // sceNetEpollAbort: wakes every thread blocked in wait(), which then returns SCE_NET_ERROR_EINTR (like an
    // aborted socket operation).
    int abort(int flags);

    epoll_abort::State abort_state;
};

typedef std::shared_ptr<Epoll> EpollPtr;
