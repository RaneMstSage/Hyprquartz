#include <hyprutils/os/Semaphore.hpp>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <format>

#if defined(__APPLE__)
#include <hyprutils/os/darwin/RuntimeDir.hpp>
#endif

using namespace Hyprutils;
using namespace Hyprutils::OS;

CSemaphore::CSemaphore(const char* name) {
#if defined(__APPLE__)
    const auto RUNTIME_DIR = Darwin::resolveRuntimeDir(true);
    if (!RUNTIME_DIR)
        return;

    m_fd = open(std::format("{}/.hu_{}.lock", *RUNTIME_DIR, name).c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
#else
    m_fd = shm_open(std::format("/hu_{}", name).c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
#endif

    if (m_fd == -1)
        return;

    while (flock(m_fd, LOCK_EX) == -1) {
        if (errno != EINTR) {
            close(m_fd);
            m_fd = -1;
            return;
        }
    }
}

CSemaphore::~CSemaphore() {
    if (m_fd != -1)
        close(m_fd);
}
