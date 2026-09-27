#if defined(__APPLE__)

#define _DARWIN_UNLIMITED_SELECT

#include <hyprutils/os/darwin/PollMask.hpp>

#include <fcntl.h>
#include <libproc.h>
#include <poll.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/proc_info.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

using namespace Hyprutils::OS;

int Darwin::kqueueQuery(int fd, int16_t filter, struct kevent* result, int queryKqueue) {
    const bool shared = queryKqueue >= 0;
    const int  kq     = shared ? queryKqueue : kqueue();
    if (kq < 0)
        return -1;

    struct kevent change;
    EV_SET(&change, fd, filter, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    const struct timespec timeout = {.tv_sec = 0, .tv_nsec = 0};
    const int             ret     = kevent(kq, &change, 1, result, 1, &timeout);
    if (shared) {
        struct kevent removal;
        EV_SET(&removal, fd, filter, EV_DELETE, 0, 0, nullptr);
        kevent(kq, &removal, 1, nullptr, 0, nullptr);
    } else
        close(kq);

    if (ret < 0)
        return -1;
    if (ret == 0 || (result->flags & EV_ERROR))
        return 0;
    return 1;
}

int Darwin::selectReady(int fd, bool write) {
    std::vector<fd_mask> set(howmany(fd + 1, NFDBITS), 0);
    FD_SET(fd, reinterpret_cast<fd_set*>(set.data()));
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
    return select(fd + 1, write ? nullptr : reinterpret_cast<fd_set*>(set.data()), write ? reinterpret_cast<fd_set*>(set.data()) : nullptr, nullptr, &timeout);
}

Darwin::SDescriptorInfo Darwin::describeDescriptor(int fd) {
    SDescriptorInfo info;

    struct stat st;
    if (fstat(fd, &st) != 0)
        return info;

    const int flags = fcntl(fd, F_GETFL);
    info.accmode    = flags >= 0 ? (flags & O_ACCMODE) : -1;

    if (S_ISSOCK(st.st_mode)) {
        info.kind = eDescriptorKind::SOCKET;
        return info;
    }

    if (S_ISFIFO(st.st_mode)) {
        struct kqueue_fdinfo kqInfo = {};
        info.kind = proc_pidfdinfo(getpid(), fd, PROC_PIDFDKQUEUEINFO, &kqInfo, sizeof(kqInfo)) >= static_cast<int>(sizeof(kqInfo)) ? eDescriptorKind::KQUEUE : eDescriptorKind::PIPE;
        return info;
    }

    if (isatty(fd)) {
        struct kevent readEvent = {};
        kqueueQuery(fd, EVFILT_READ, &readEvent);
        if (readEvent.flags & EV_ERROR)
            info.kind = eDescriptorKind::TTY_SELECT;
        else {
            char slaveName[128];
            info.kind = ioctl(fd, TIOCPTYGNAME, slaveName) == 0 ? eDescriptorKind::TTY_MASTER : eDescriptorKind::TTY;
        }
        return info;
    }

    if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || (st.st_mode & S_IFMT) == 0) {
        info.kind = eDescriptorKind::REJECT;
        return info;
    }

    if (S_ISCHR(st.st_mode)) {
        static const dev_t RANDOM_RDEV = [] {
            struct stat random;
            return stat("/dev/random", &random) == 0 && S_ISCHR(random.st_mode) ? random.st_rdev : static_cast<dev_t>(-1);
        }();
        if (st.st_rdev == RANDOM_RDEV) {
            info.kind = eDescriptorKind::RANDOM;
            return info;
        }

        struct kevent readEvent = {};
        kqueueQuery(fd, EVFILT_READ, &readEvent);
        info.kind = (readEvent.flags & EV_ERROR) ? eDescriptorKind::REJECT : eDescriptorKind::OTHER;
        return info;
    }

    info.kind = eDescriptorKind::OTHER;
    return info;
}

uint8_t Darwin::linuxPoll(int fd, const SDescriptorInfo& info, uint8_t wanted, int queryKqueue) {
    uint8_t bits = 0;

    switch (info.kind) {
        case eDescriptorKind::INVALID: return bits;

        case eDescriptorKind::SOCKET: {
            if (wanted & (LINUX_POLL_OUT | LINUX_POLL_ERR | LINUX_POLL_HUP | LINUX_POLL_RDHUP)) {
                struct socket_fdinfo socketInfo = {};
                if (proc_pidfdinfo(getpid(), fd, PROC_PIDFDSOCKETINFO, &socketInfo, sizeof(socketInfo)) < static_cast<int>(sizeof(socketInfo)))
                    bits |= LINUX_POLL_HUP | LINUX_POLL_ERR;
                else {
                    const auto state     = socketInfo.psi.soi_state;
                    const bool listening = socketInfo.psi.soi_options & SO_ACCEPTCONN;
                    const bool stream    = socketInfo.psi.soi_type == SOCK_STREAM || socketInfo.psi.soi_type == SOCK_SEQPACKET;

                    if (socketInfo.psi.soi_error != 0)
                        bits |= LINUX_POLL_ERR;

                    if ((state & SOI_S_CANTRCVMORE) && (state & SOI_S_CANTSENDMORE))
                        bits |= LINUX_POLL_HUP;

                    if (stream && !listening && !(state & SOI_S_ISCONNECTED))
                        bits |= LINUX_POLL_HUP;

                    if (state & SOI_S_CANTRCVMORE)
                        bits |= LINUX_POLL_RDHUP;

                    if ((wanted & LINUX_POLL_OUT) && !listening) {
                        struct kevent writeEvent = {};
                        if (!(state & SOI_S_ISCONNECTED) || kqueueQuery(fd, EVFILT_WRITE, &writeEvent, queryKqueue) > 0)
                            bits |= LINUX_POLL_OUT;
                    }
                }
            }

            if (wanted & LINUX_POLL_IN) {
                pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
                if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
                    bits |= LINUX_POLL_IN;
            }

            return bits;
        }

        case eDescriptorKind::PIPE:
        case eDescriptorKind::KQUEUE: {
            const bool readSide   = info.accmode >= 0 && info.accmode != O_WRONLY;
            const bool writeSide  = info.kind == eDescriptorKind::PIPE && info.accmode >= 0 && info.accmode != O_RDONLY;
            const bool writeQuery = writeSide && ((wanted & LINUX_POLL_OUT) || (info.accmode == O_WRONLY && (wanted & (LINUX_POLL_ERR | LINUX_POLL_HUP))));

            if (readSide) {
                struct kevent readEvent = {};
                const int     readRet   = kqueueQuery(fd, EVFILT_READ, &readEvent, queryKqueue);

                if (readRet < 0)
                    bits |= LINUX_POLL_HUP;
                else {
                    if (readRet > 0 && readEvent.data > 0)
                        bits |= LINUX_POLL_IN;
                    else if (selectReady(fd, false) != 0)
                        bits |= LINUX_POLL_HUP;
                    if (readRet > 0 && (readEvent.flags & EV_EOF))
                        bits |= LINUX_POLL_HUP;
                }
            }

            if (writeQuery) {
                struct kevent writeEvent = {};
                const int     writeRet   = kqueueQuery(fd, EVFILT_WRITE, &writeEvent, queryKqueue);

                if (writeRet < 0)
                    bits |= LINUX_POLL_HUP;
                else if (writeRet > 0) {
                    if (wanted & LINUX_POLL_OUT)
                        bits |= LINUX_POLL_OUT;
                    if ((writeEvent.flags & EV_EOF) && info.accmode == O_WRONLY)
                        bits |= LINUX_POLL_ERR;
                }
            }

            return bits;
        }

        case eDescriptorKind::TTY: {
            struct kevent readEvent = {};
            const int     readRet   = kqueueQuery(fd, EVFILT_READ, &readEvent, queryKqueue);

            if (readRet < 0)
                return bits | LINUX_POLL_HUP;

            if (readRet > 0 && (readEvent.flags & EV_EOF))
                return bits | LINUX_POLL_IN | LINUX_POLL_OUT | LINUX_POLL_ERR | LINUX_POLL_HUP;

            if (readRet > 0 && readEvent.data > 0)
                bits |= LINUX_POLL_IN;

            if (wanted & LINUX_POLL_OUT) {
                struct kevent writeEvent = {};
                if (kqueueQuery(fd, EVFILT_WRITE, &writeEvent, queryKqueue) > 0)
                    bits |= LINUX_POLL_OUT;
            }

            return bits;
        }

        case eDescriptorKind::TTY_MASTER: {
            struct kevent readEvent = {};
            const int     readRet   = kqueueQuery(fd, EVFILT_READ, &readEvent, queryKqueue);

            if (readRet < 0)
                return bits | LINUX_POLL_HUP;

            if (readRet > 0 && readEvent.data > 0)
                bits |= LINUX_POLL_IN;

            if (readRet > 0 && (readEvent.flags & EV_EOF))
                bits |= LINUX_POLL_HUP;

            if (wanted & LINUX_POLL_OUT) {
                struct kevent writeEvent = {};
                if (kqueueQuery(fd, EVFILT_WRITE, &writeEvent, queryKqueue) > 0)
                    bits |= LINUX_POLL_OUT;
            }

            return bits;
        }

        case eDescriptorKind::RANDOM: return bits | LINUX_POLL_IN;

        case eDescriptorKind::TTY_SELECT: {
            const int ret = selectReady(fd, false);

            if (ret < 0)
                return bits | LINUX_POLL_HUP;

            if (ret > 0) {
                int available = 0;
                if (!(ioctl(fd, FIONREAD, &available) == 0 && available > 0))
                    return bits | LINUX_POLL_IN | LINUX_POLL_OUT | LINUX_POLL_ERR | LINUX_POLL_HUP;
                bits |= LINUX_POLL_IN;
            }

            if ((wanted & LINUX_POLL_OUT) && selectReady(fd, true) > 0)
                bits |= LINUX_POLL_OUT;

            return bits;
        }

        case eDescriptorKind::REJECT:
        case eDescriptorKind::OTHER: {
            pollfd    pfd = {.fd = fd, .events = POLLIN, .revents = 0};
            const int ret = poll(&pfd, 1, 0);

            if (ret < 0)
                bits |= LINUX_POLL_HUP;
            else if (ret > 0) {
                if (pfd.revents & (POLLIN | POLLNVAL))
                    bits |= LINUX_POLL_IN;
                if (pfd.revents & POLLHUP)
                    bits |= LINUX_POLL_HUP;
                if (pfd.revents & POLLERR)
                    bits |= LINUX_POLL_ERR;
            }

            if (wanted & LINUX_POLL_OUT) {
                pollfd writePfd = {.fd = fd, .events = POLLOUT, .revents = 0};
                if (poll(&writePfd, 1, 0) > 0 && (writePfd.revents & (POLLOUT | POLLNVAL)))
                    bits |= LINUX_POLL_OUT;
            }

            return bits;
        }
    }

    return bits;
}

#endif
