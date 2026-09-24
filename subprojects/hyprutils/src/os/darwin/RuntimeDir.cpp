#if defined(__APPLE__)

#include <hyprutils/os/darwin/RuntimeDir.hpp>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <format>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace Hyprutils::OS;

constexpr size_t RUNTIME_DIR_CHILD_RESERVE = 128;

static std::string errnoMessage(const char* what, const std::string& path) {
    return std::format("{} {}: {}", what, path, std::strerror(errno));
}

static std::string parentOf(const std::string& path) {
    const auto POS = path.find_last_of('/');
    return POS == std::string::npos || POS == 0 ? std::string{"/"} : path.substr(0, POS);
}

static std::expected<void, std::string> ensureOwnedDirectory(const std::string& path, bool create, bool makePrivate) {
    struct stat st = {};
    if (lstat(path.c_str(), &st) != 0) {
        if (errno != ENOENT || !create)
            return std::unexpected(errnoMessage("cannot stat", path));
        if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
            return std::unexpected(errnoMessage("cannot create", path));
        if (lstat(path.c_str(), &st) != 0)
            return std::unexpected(errnoMessage("cannot stat", path));
    }

    if (!S_ISDIR(st.st_mode))
        return std::unexpected(std::format("{} is not a directory", path));

    if (st.st_uid != getuid())
        return std::unexpected(std::format("{} is owned by uid {}, not {}", path, st.st_uid, getuid()));

    if (makePrivate && (st.st_mode & 077) != 0 && chmod(path.c_str(), 0700) != 0)
        return std::unexpected(errnoMessage("cannot make private", path));

    return {};
}

std::string Darwin::runtimeDirLinkPath() {
    return std::format("/tmp/hyprquartz-{}", getuid());
}

std::expected<std::string, std::string> Darwin::runtimeDirTargetPath() {
    long bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufferSize <= 0)
        bufferSize = 16384;

    std::vector<char> buffer(static_cast<size_t>(bufferSize));
    struct passwd     pw     = {};
    struct passwd*    result = nullptr;
    if (getpwuid_r(getuid(), &pw, buffer.data(), buffer.size(), &result) != 0 || !result || !pw.pw_dir || pw.pw_dir[0] != '/')
        return std::unexpected(std::format("no home directory for uid {}", getuid()));

    return std::format("{}/Library/Application Support/Hyprquartz/runtime", pw.pw_dir);
}

std::expected<std::string, std::string> Darwin::ensureRuntimeDirAt(const std::string& link, const std::string& target, bool create) {
    if (target.empty() || target[0] != '/')
        return std::unexpected(std::format("runtime target {} is not an absolute path", target));

    if (target.size() + RUNTIME_DIR_CHILD_RESERVE >= PATH_MAX)
        return std::unexpected(std::format("runtime target {} is too long ({} chars, limit {})", target, target.size(), PATH_MAX - RUNTIME_DIR_CHILD_RESERVE - 1));

    const auto PARENT      = parentOf(target);
    const auto GRANDPARENT = parentOf(PARENT);

    if (auto r = ensureOwnedDirectory(GRANDPARENT, create, false); !r)
        return std::unexpected(r.error());
    if (auto r = ensureOwnedDirectory(PARENT, create, true); !r)
        return std::unexpected(r.error());
    if (auto r = ensureOwnedDirectory(target, create, true); !r)
        return std::unexpected(r.error());

    for (int attempt = 0; attempt < 3; ++attempt) {
        struct stat st = {};
        if (lstat(link.c_str(), &st) != 0) {
            if (errno != ENOENT)
                return std::unexpected(errnoMessage("cannot stat", link));
            if (!create)
                return std::unexpected(std::format("{} does not exist", link));
            if (symlink(target.c_str(), link.c_str()) != 0 && errno != EEXIST)
                return std::unexpected(errnoMessage("cannot create symlink", link));
            continue;
        }

        if (st.st_uid != getuid())
            return std::unexpected(std::format("{} is owned by uid {}, not {}", link, st.st_uid, getuid()));

        if (S_ISLNK(st.st_mode)) {
            char          buffer[PATH_MAX];
            const ssize_t len = readlink(link.c_str(), buffer, sizeof(buffer) - 1);
            if (len < 0)
                return std::unexpected(errnoMessage("cannot read symlink", link));
            buffer[len] = '\0';
            if (target == buffer)
                return link;
            if (!create)
                return std::unexpected(std::format("{} points to {}, not {}", link, buffer, target));
            if (unlink(link.c_str()) != 0)
                return std::unexpected(errnoMessage("cannot replace symlink", link));
            continue;
        }

        if (!create)
            return std::unexpected(std::format("{} is not a symlink", link));

        if (S_ISDIR(st.st_mode)) {
            if (rmdir(link.c_str()) != 0)
                return std::unexpected(errnoMessage("is a directory and cannot be replaced", link));
            continue;
        }

        if (unlink(link.c_str()) != 0)
            return std::unexpected(errnoMessage("cannot replace", link));
    }

    return std::unexpected(std::format("{} could not be set up", link));
}

std::expected<std::string, std::string> Darwin::ensureRuntimeDir(bool create) {
    static const auto TARGET = runtimeDirTargetPath();
    if (!TARGET)
        return std::unexpected(TARGET.error());

    return ensureRuntimeDirAt(runtimeDirLinkPath(), *TARGET, create);
}

std::expected<std::string, std::string> Darwin::resolveRuntimeDir(bool create) {
    const auto XDG_RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");
    if (XDG_RUNTIME_DIR && XDG_RUNTIME_DIR[0] != 0 && runtimeDirLinkPath() != XDG_RUNTIME_DIR)
        return std::string{XDG_RUNTIME_DIR};

    return ensureRuntimeDir(create);
}

std::expected<void, std::string> Darwin::exportRuntimeDir() {
    const auto XDG_RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");
    if (XDG_RUNTIME_DIR && XDG_RUNTIME_DIR[0] != 0 && runtimeDirLinkPath() != XDG_RUNTIME_DIR)
        return {};

    const auto RUNTIME_DIR = ensureRuntimeDir(true);
    if (!RUNTIME_DIR)
        return std::unexpected(RUNTIME_DIR.error());

    if (setenv("XDG_RUNTIME_DIR", RUNTIME_DIR->c_str(), 1) != 0)
        return std::unexpected(errnoMessage("cannot set XDG_RUNTIME_DIR to", *RUNTIME_DIR));

    return {};
}

#endif
