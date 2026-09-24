#if defined(__APPLE__)
#define _DARWIN_UNLIMITED_SELECT
#endif

#include <cstdlib>
#include <hyprutils/os/FileDescriptor.hpp>
#include <fcntl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/proc_info.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <vector>
#endif

using namespace Hyprutils::OS;

#if defined(__APPLE__)
static int kqueueQuery(int fd, int16_t filter, struct kevent* result) {
    int kq = kqueue();
    if (kq < 0)
        return -1;

    struct kevent change;
    EV_SET(&change, fd, filter, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    const struct timespec timeout = {.tv_sec = 0, .tv_nsec = 0};
    const int             ret     = kevent(kq, &change, 1, result, 1, &timeout);
    close(kq);

    if (ret < 0)
        return -1;
    if (ret == 0 || (result->flags & EV_ERROR))
        return 0;
    return 1;
}
#endif

CFileDescriptor::CFileDescriptor(int const fd) : m_fd(fd) {}

CFileDescriptor::CFileDescriptor(CFileDescriptor&& other) : m_fd(std::exchange(other.m_fd, -1)) {}

CFileDescriptor& CFileDescriptor::operator=(CFileDescriptor&& other) {
    if (this == &other) // Shit will go haywire if there is duplicate ownership
        abort();

    reset();
    m_fd = std::exchange(other.m_fd, -1);
    return *this;
}

CFileDescriptor::~CFileDescriptor() {
    reset();
}

bool CFileDescriptor::isValid() const {
    return m_fd != -1;
}

int CFileDescriptor::get() const {
    return m_fd;
}

int CFileDescriptor::getFlags() const {
    return fcntl(m_fd, F_GETFD);
}

bool CFileDescriptor::setFlags(int flags) {
    return fcntl(m_fd, F_SETFD, flags) != -1;
}

int CFileDescriptor::take() {
    return std::exchange(m_fd, -1);
}

void CFileDescriptor::reset() {
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
}

CFileDescriptor CFileDescriptor::duplicate(int flags) const {
    if (m_fd == -1)
        return {};

    return CFileDescriptor{fcntl(m_fd, flags, 0)};
}

bool CFileDescriptor::isClosed() const {
    return isClosed(m_fd);
}

bool CFileDescriptor::isReadable() const {
    return isReadable(m_fd);
}

bool CFileDescriptor::isClosed(int fd) {
#if defined(__APPLE__)
    struct stat st;
    if (fstat(fd, &st) != 0)
        return false;

    if (S_ISSOCK(st.st_mode)) {
        struct socket_fdinfo info = {};
        if (proc_pidfdinfo(getpid(), fd, PROC_PIDFDSOCKETINFO, &info, sizeof(info)) < static_cast<int>(sizeof(info)))
            return true;

        if (info.psi.soi_error != 0)
            return true;

        if ((info.psi.soi_state & SOI_S_CANTRCVMORE) && (info.psi.soi_state & SOI_S_CANTSENDMORE))
            return true;

        if ((info.psi.soi_type == SOCK_STREAM || info.psi.soi_type == SOCK_SEQPACKET) && !(info.psi.soi_options & SO_ACCEPTCONN) && !(info.psi.soi_state & SOI_S_ISCONNECTED))
            return true;

        return false;
    }

    if (S_ISFIFO(st.st_mode)) {
        const int flags = fcntl(fd, F_GETFL);
        if (flags >= 0 && (flags & O_ACCMODE) != O_WRONLY) {
            struct kevent readEvent = {};
            const int     readRet   = kqueueQuery(fd, EVFILT_READ, &readEvent);

            if (readRet < 0)
                return true;

            if (!(readRet > 0 && readEvent.data > 0)) {
                std::vector<fd_mask> readSet(howmany(fd + 1, NFDBITS), 0);
                FD_SET(fd, reinterpret_cast<fd_set*>(readSet.data()));
                struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
                const int      ret     = select(fd + 1, reinterpret_cast<fd_set*>(readSet.data()), nullptr, nullptr, &timeout);

                if (ret != 0)
                    return true;
            }
        }
    }

    if (isatty(fd)) {
        struct kevent readEvent = {};
        const int     readRet   = kqueueQuery(fd, EVFILT_READ, &readEvent);

        if (readRet < 0)
            return true;

        if (readRet == 0 && (readEvent.flags & EV_ERROR)) {
            std::vector<fd_mask> readSet(howmany(fd + 1, NFDBITS), 0);
            FD_SET(fd, reinterpret_cast<fd_set*>(readSet.data()));
            struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
            const int      ret     = select(fd + 1, reinterpret_cast<fd_set*>(readSet.data()), nullptr, nullptr, &timeout);

            if (ret < 0)
                return true;

            if (ret == 0)
                return false;

            int available = 0;
            return !(ioctl(fd, FIONREAD, &available) == 0 && available > 0);
        }

        return readRet > 0 && (readEvent.flags & EV_EOF);
    }
#endif

    pollfd pfd = {
        .fd      = fd,
        .events  = POLLIN,
        .revents = 0,
    };

    if (poll(&pfd, 1, 0) < 0)
        return true;

    return pfd.revents & (POLLHUP | POLLERR);
}

bool CFileDescriptor::isReadable(int fd) {
#if defined(__APPLE__)
    struct stat st;
    if (fstat(fd, &st) != 0)
        return false;

    if (S_ISFIFO(st.st_mode)) {
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || (flags & O_ACCMODE) == O_WRONLY)
            return false;

        struct kevent readEvent = {};
        return kqueueQuery(fd, EVFILT_READ, &readEvent) > 0 && readEvent.data > 0;
    }

    if (isatty(fd)) {
        struct kevent readEvent = {};
        const int     readRet   = kqueueQuery(fd, EVFILT_READ, &readEvent);

        if (readRet == 0 && (readEvent.flags & EV_ERROR)) {
            std::vector<fd_mask> readSet(howmany(fd + 1, NFDBITS), 0);
            FD_SET(fd, reinterpret_cast<fd_set*>(readSet.data()));
            struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
            const int      ret     = select(fd + 1, reinterpret_cast<fd_set*>(readSet.data()), nullptr, nullptr, &timeout);

            return ret > 0;
        }

        return readRet > 0 && (readEvent.data > 0 || (readEvent.flags & EV_EOF));
    }

    if (!S_ISSOCK(st.st_mode)) {
        pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};

        return poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLNVAL));
    }
#endif

    pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};

    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}
