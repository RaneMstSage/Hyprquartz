#include "Syscalls.hpp"

#include <poll.h>

#if defined(__APPLE__)
#include <hyprutils/os/darwin/PollMask.hpp>
#endif

using namespace Hyprwire;

namespace {
    Hyprwire::Syscalls::SHooks g_hooks;
}

int Hyprwire::Syscalls::poll(pollfd* fds, nfds_t nfds, int timeout) {
    if (g_hooks.poll)
        return g_hooks.poll(fds, nfds, timeout);

#if defined(__APPLE__)
    const int ret = ::poll(fds, nfds, timeout);
    if (ret <= 0)
        return ret;

    int ready = 0;
    for (nfds_t i = 0; i < nfds; ++i) {
        if (fds[i].fd < 0)
            continue;

        uint8_t wanted = Hyprutils::OS::Darwin::LINUX_POLL_ERR | Hyprutils::OS::Darwin::LINUX_POLL_HUP;
        if (fds[i].events & POLLIN)
            wanted |= Hyprutils::OS::Darwin::LINUX_POLL_IN;
        if (fds[i].events & POLLOUT)
            wanted |= Hyprutils::OS::Darwin::LINUX_POLL_OUT;

        const auto bits    = Hyprutils::OS::Darwin::linuxPoll(fds[i].fd, Hyprutils::OS::Darwin::describeDescriptor(fds[i].fd), wanted);
        short      revents = fds[i].revents & POLLNVAL;
        if (bits & Hyprutils::OS::Darwin::LINUX_POLL_IN)
            revents |= POLLIN;
        if (bits & Hyprutils::OS::Darwin::LINUX_POLL_OUT)
            revents |= POLLOUT;
        if (bits & Hyprutils::OS::Darwin::LINUX_POLL_ERR)
            revents |= POLLERR;
        if (bits & Hyprutils::OS::Darwin::LINUX_POLL_HUP)
            revents |= POLLHUP;

        fds[i].revents = revents;
        if (revents)
            ++ready;
    }

    return ready;
#else
    return ::poll(fds, nfds, timeout);
#endif
}

ssize_t Hyprwire::Syscalls::sendmsg(int sockfd, const msghdr* msg, int flags) {
    if (g_hooks.sendmsg)
        return g_hooks.sendmsg(sockfd, msg, flags);

    return ::sendmsg(sockfd, msg, flags);
}

ssize_t Hyprwire::Syscalls::recvmsg(int sockfd, msghdr* msg, int flags) {
    if (g_hooks.recvmsg)
        return g_hooks.recvmsg(sockfd, msg, flags);

    return ::recvmsg(sockfd, msg, flags);
}

void Hyprwire::Syscalls::setHooks(const SHooks& hooks) {
    g_hooks = hooks;
}

void Hyprwire::Syscalls::resetHooks() {
    g_hooks = {};
}
