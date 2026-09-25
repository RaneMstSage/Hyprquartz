#pragma once

#if defined(__APPLE__)

#include <cstdint>

struct kevent;

namespace Hyprutils::OS::Darwin {
    enum class eDescriptorKind : uint8_t {
        INVALID,
        OTHER,
        REJECT,
        SOCKET,
        PIPE,
        KQUEUE,
        TTY,
        TTY_SELECT,
        TTY_MASTER,
        RANDOM,
    };

    enum eLinuxPoll : uint8_t {
        LINUX_POLL_IN    = (1 << 0),
        LINUX_POLL_OUT   = (1 << 1),
        LINUX_POLL_ERR   = (1 << 2),
        LINUX_POLL_HUP   = (1 << 3),
        LINUX_POLL_RDHUP = (1 << 4),
    };

    struct SDescriptorInfo {
        eDescriptorKind kind    = eDescriptorKind::INVALID;
        int             accmode = -1;
    };

    int             kqueueQuery(int fd, int16_t filter, struct kevent* result, int queryKqueue = -1);
    int             selectReady(int fd, bool write);
    SDescriptorInfo describeDescriptor(int fd);
    uint8_t         linuxPoll(int fd, const SDescriptorInfo& info, uint8_t wanted, int queryKqueue = -1);
}

#endif
