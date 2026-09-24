# Hyprquartz: Hyprland ported to macOS

## Paths
- Hyprquartz (our port, the only place you write): /Volumes/Data/projects/Hyprquartz
- Hyprland upstream reference (READ ONLY): /Volumes/Data/projects/Hyprland
- hyprutils upstream reference (READ ONLY): /Volumes/Data/projects/hyprutils
- hyprutils port: subprojects/hyprutils

## What this port is
This is a FAITHFUL PORT. Upstream is the specification.
1. Everything ports: every file, function, class, enum, option, comment and line.
2. NEVER cut, drop, skip, stub or simplify anything because it "looks unused", "isn't needed yet" or "can be added later".
3. Only Linux/Wayland-specific parts change (Wayland, aquamarine, DRM/KMS, libinput, GLES/EGL, XWayland, systemd, Linux-only syscalls/headers). Each gets an EXPLICIT, NAMED macOS replacement. "Drop" is not a replacement.
4. Linux-specific behavior is porting work, not a note. Anything that works differently or silently stops working on macOS needs a macOS replacement.
5. For any real Linux/Wayland code or Linux-only API: STOP, describe what it does upstream, propose the replacement, wait for approval.
6. A Linux-derived name with no Linux code behind it (e.g. applyFromWlr, AQ_FORMAT_*) ports verbatim without stopping; list it in the file's report.
7. Do not fix or improve upstream, including typos and suspected bugs. Report suspected bugs; port as-is.
8. Keep upstream comments verbatim. Add no comments of your own. Match upstream formatting.

## Corner-cutting is forbidden
- Leaving anything out because you judge it unimportant, unused, legacy, debug-only, test-only or Linux-only.
- Porting a subset of enum values, #defines, overloads, members, functions, options, keywords, test cases or #if branches.
- Removing an overload because another "covers it".
- Stubbing: empty bodies, TODOs, placeholder returns, #if 0, (void) where upstream has real code.
- Abbreviating: "rest unchanged", "etc.", summarizing instead of writing the file.
- Simplifying, merging, reordering, renaming, or swapping upstream helpers for std:: equivalents.
- Deleting comments, upstream copy-paste mistakes included.
- Skipping error handling, asserts, logging or edge cases.
If a file is too long for one response, stop at a clean boundary and say where. If you think "not needed", "skip", "for now" or "simplified", stop and ask.

## Evidence rule
Every claim about macOS behavior needs evidence: a quoted macOS man page, an SDK header path and line, an Apple documentation URL, or an approved test result. Otherwise label it UNVERIFIED and say how to verify it. Never conclude "this can't work on macOS" from an unverified claim. "It doesn't work on macOS" is never a final answer; the feature still ports with a macOS replacement that keeps upstream's inputs, outputs and failure cases.

## Config contract
hyprland.conf (hyprlang) and hyprland.lua (Lua 5.5, hl.* API) stay identical: same option names, types, defaults, keywords, dispatchers and Lua functions. Options with no macOS meaning still register and parse without error. Implementation may diverge; the interface may not.

## Naming rule
- Names other programs look up stay Hyprland's: HYPRLAND_INSTANCE_SIGNATURE, HYPRLAND_CMD, .socket.sock, .socket2.sock, hyprland.conf, hyprland.lua.
- Names that identify the program become Hyprquartz: binary, log files (hyprquartz.log / hyprquartzd.log), banner, --version output.

## Decisions already made
- Build: CMake, C++26 (CMAKE_CXX_STANDARD 26, CMAKE_CXX_STANDARD_REQUIRED ON), AppleClang 17, CLion, target `hyprquartz`.
- hyprutils: ported by hand into subprojects/hyprutils; add_subdirectory before Hyprquartz's compile options; target_link_libraries(hyprquartz PRIVATE hyprutils).
- hyprutils CMake: CMAKE_SOURCE_DIR/CMAKE_BINARY_DIR -> CMAKE_CURRENT_SOURCE_DIR/CMAKE_CURRENT_BINARY_DIR; Darwin added to the kqueue branch (Kqueue.cpp works on macOS as-is; Epoll.cpp ported verbatim but not compiled, as on the BSDs); tests link ASan via -fsanitize=address.
- <linux/input-event-codes.h> vendored verbatim at compat/linux/input-event-codes.h, compat/ is an include dir.
- includes.hpp: libinput -> IOKit HID (IOHIDManager) + Quartz Event Services (CGEventTap); wayland-server-core -> CFRunLoop; ApplicationServices for Accessibility (AXUIElement/AXObserver); XWAYLAND defined as false.
- Math: wl_output_transform -> hyprutils eTransform; wlTransformToHyprutils() -> cgRotationToHyprutils() using CGDisplayRotation() degrees (direction still to be verified).
- eRenderStage ports verbatim; emit points mapped when the renderer is designed.
- Runtime folder (XDG_RUNTIME_DIR replacement): /tmp/hyprquartz-<uid> is a symlink to ~/Library/Application Support/Hyprquartz/runtime. On every use, lstat/readlink check (symlink, owned by us, expected target), refuse if it fails. Hyprquartz sets XDG_RUNTIME_DIR to /tmp/hyprquartz-<uid> at startup if unset. Startup cleanup of instance folders and lock files whose process is dead. Reason: macOS tmp_cleaner deletes regular files in /tmp not touched for 3 days, $TMPDIR is also cleaned, and sun_path is limited to 104 bytes (max socket path is 101 chars). Verified by probe.
- Semaphore.cpp: flock on POSIX shm fds fails on macOS (ENOTSUP, verified by probe). Replacement: flock on a regular lock file, <runtime folder>/.hu_<name>.lock.
- I18nEngine.cpp: if LC_ALL/LC_MESSAGES/LANG is set and not C/POSIX, keep upstream's std::locale(""); otherwise use the first entry of CFLocaleCopyPreferredLanguages() with - changed to _. Keep script tags (zh_Hans_CN). hyprutils links -framework CoreFoundation.
- Logger.cpp: Apple libc++ has no time zone database. Add an #elif defined(__APPLE__) branch using localtime_r + tm_gmtoff for local time; both upstream branches stay verbatim; no padding to 9 digits.
- Path.cpp: no change; XDG vars unset on macOS resolve to ~/.config/hypr, matching the Linux fleet.
- Numeric.hpp: floating-point from_chars unavailable below macOS 26 in Apple's libc++; upstream's strtod fallback enabled via _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT on lines 8 and 49.
- FileDescriptor.cpp isClosed()/isReadable(): macOS replacement under #if defined(__APPLE__) reproducing Linux poll() results: fstat type dispatch; proc_pidfdinfo(PROC_PIDFDSOCKETINFO) for sockets; kqueue EVFILT_READ for FIFOs (write-only ends not readable) plus a zero-timeout select() on non-write-only FIFOs with no pending bytes to detect "all writers gone" (Q2); kqueue for ttys, and for ttys where kqueue can't attach (/dev/tty, EV_ERROR) a zero-timeout select() read check (readable = ready; closed = ready with FIONREAD showing no data, i.e. hung up); every select() uses _DARWIN_UNLIMITED_SELECT with a descriptor set sized for the fd, so no FD_SETSIZE limit; POLLNVAL treated as readable for non-pollable descriptors; upstream code verbatim for non-Apple.
- proc_pidfdinfo is a private API (libproc.h: "private interfaces to obtain process information. These interfaces are subject to change in future releases."). Re-verify it after every macOS update by building and running probes/poll/poll_probe.cpp: every "new:" column must still match the Linux column.

## Known macOS limitations
- FileDescriptor.cpp isClosed() on a named FIFO: XNU only exposes "all writers have gone" through select() while the buffer is empty and before the first EOF read (implemented). Linux's POLLHUP in the other cases cannot be reproduced on macOS. Measured with probes/poll/poll_probe.cpp: Q5, a writer wrote then left with data unread (Linux closed=true readable=true; macOS closed=false readable=true); Q6, EOF already read once (Linux closed=true readable=false; macOS closed=false readable=false). Evidence: XNU fifo_vnops.c fifo_close_internal / fifo_read, vfs_vnops.c filt_vnode_common. Re-check with probes/poll/poll_probe.cpp.

## Port-only files
- subprojects/hyprutils/include/hyprutils/os/darwin/RuntimeDir.hpp and subprojects/hyprutils/src/os/darwin/RuntimeDir.cpp: the macOS runtime folder (XDG_RUNTIME_DIR replacement: /tmp/hyprquartz-<uid> symlink to ~/Library/Application Support/Hyprquartz/runtime, checked on every use), in hyprutils so that hyprutils (Semaphore.cpp, ProcLock.cpp), Hyprquartz and hyprctl share one implementation; entirely inside #if defined(__APPLE__), so they compile to nothing on other platforms.

## Pending (do when the file is reached)
- Hyprland main.cpp / CCompositor (src/Compositor.cpp): on Apple, call Hyprutils::OS::Darwin::exportRuntimeDir() at startup before any threads exist (setenv is not thread-safe), stopping with an error if it fails (as upstream main.cpp:202 does for an unset XDG_RUNTIME_DIR). Then clean up dead instance folders, since nothing wipes the runtime folder at reboot: for each <runtime>/hypr/<signature>/, remove it if hyprland.lock is missing or the PID on its first line is not alive. ProcLock lock files are already cleaned by upstream readLockFile(); Semaphore .hu_<name>.lock files need no cleanup.

## Open decisions (raise when reached; do not decide)
- Renderer for borders/shadows/blur/screen_shader: metal-cpp (+ glslang -> SPIRV-Cross) vs CoreAnimation.
- Keyboard mapping: macOS kVK_* -> evdev KEY_*, and xkbcommon for keysyms.
- How Hyprquartz is launched (LaunchAgent), including environment such as XDG_CONFIG_HOME. When PATH is unset (launchd), macOS execvp only searches /usr/bin:/bin (_PATH_DEFPATH, paths.h:65), so programs launched by exec-once / hl.dsp.exec_cmd from Homebrew or other locations would not be found; the launch design must provide PATH.

## Suspected upstream bugs (ported as-is, to report upstream)
- hyprutils src/math/Mat3x3.cpp:33: mat.size() < i condition is backwards.
- hyprutils src/math/Region.cpp:214: closestPoint distance measured from origin, not from vec.
- hyprutils src/math/Vector2D.cpp: normalize() on {0, 0} produces NaN.
- hyprutils src/string/VarList.cpp: with removeEmpty, skipped pieces don't advance pos, so the lastArgNo remainder starts too early.
- hyprutils src/string/VarList2.cpp: lastArgNo is never used by construct() (and construct()'s `in` parameter is unused).
- hyprutils src/string/VarList2.cpp: m_args string_views into m_copyStrings elements can dangle when the vector grows (short-string optimization; affects more strings on Apple libc++, up to 22 chars, than libstdc++, up to 15).
- hyprutils src/string/VarList2.cpp: escaped pieces are added even when removeEmpty is set and they trim to empty.
- hyprutils src/string/ConstVarList.cpp: with removeEmpty, skipped pieces don't advance pos, so the lastArgNo remainder starts too early (same as VarList.cpp).
- hyprutils src/string/ConstVarList.cpp: the lastArgNo remainder is a view into m_str after delimiters were replaced with 0 bytes, so it contains embedded \0 characters.
- hyprutils src/string/ConstVarList.cpp: join() with `to` greater than size() reads past the end of m_args (no bounds check).
- hyprutils src/string/VarList.cpp: join() with `to` greater than size() reads past the end of m_vArgs (no bounds check).

## How we work
- One file at a time, in dependency order. Never jump ahead. Only do what the prompt asks; no extra steps.
- Read the upstream file in full before writing. cp is fine for files with zero differences.
- After writing each file, open it as an editor tab in the Hyprquartz project with the JetBrains MCP (mcp__clion__open_file_in_editor). No launcher, no open -a.
- Never modify the reference clones. No scratch or temporary directories; everything stays inside the Hyprquartz project. Probes live in probes/ (gitignored) and need approval first.
- Do not build unless asked; I build in CLion.
- Never commit, push or change git state unless asked.
- Commit messages contain only the text I give. No Co-Authored-By, Claude-Session or any other trailers.
- Do not write to your memory unless asked.
- If anything is unclear, ask.

## Proof after every file
1. diff against upstream, every hunk explained (approved replacement or naming rule only).
2. Count comparison, upstream vs port: lines, functions, classes, enum values, #defines, options. Any lower count is a failure to fix or justify.
3. Statement: "Nothing from upstream was removed or shortened," or exactly what differs and why.
4. A short plain-language explanation of what the file contains and how it works.
