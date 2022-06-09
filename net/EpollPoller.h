/* 对epoll进行封装 */

#pragma once

#include <sys/epoll.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <vector>
#include <string.h>
#include <errno.h>

namespace net
{

    class EpollPoller
    {
    private:
        int m_epollFd;
        std::vector<struct epoll_event> m_events;

    public:
        explicit EpollPoller(int maxEvents = 1024);
        ~EpollPoller();

        bool AddFd(int fd, uint32_t events);

        bool ModFd(int fd, uint32_t events);

        bool DelFd(int fd);

        int EPollWait(int timeoutMs = -1);

        int GetEventFd(size_t i) const;

        uint32_t GetEvents(size_t i) const;
    };
    
    
    
}