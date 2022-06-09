#include "EpollPoller.h"

using namespace net;

EpollPoller::EpollPoller( int maxEvents ): m_epollFd(epoll_create(5)), m_events(maxEvents)
{
    assert(m_epollFd > 0 && m_events.size() > 0);
};
    
EpollPoller::~EpollPoller()
{
    close(m_epollFd);
}

bool EpollPoller::AddFd(int fd, uint32_t events)
{
    if(fd < 0)
        return false;
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
}

bool EpollPoller::ModFd(int fd, uint32_t events)
{
    if(fd < 0)
        return false;

    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &ev);
}

bool EpollPoller::DelFd(int fd)
{
    if(fd < 0)
        return false;
    
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    return 0 == epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, &ev);
}

int EpollPoller::EPollWait(int timeoutMs)
{
    return epoll_wait(m_epollFd, &m_events[0], static_cast<int>(m_events.size()), timeoutMs);
}

int EpollPoller::GetEventFd(size_t i) const
{
    assert(i < m_events.size() && i >= 0);
    return m_events[i].data.fd;
}

uint32_t EpollPoller::GetEvents(size_t i) const
{
    assert(i < m_events.size() && i >= 0);
    return m_events[i].events;
}