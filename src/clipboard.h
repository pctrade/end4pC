// The clipboard through wl-copy and wl-paste, with a time limit on both (a stuck selection owner must not freeze a window).
// Used by the one-line text fields of Settings and the wallpaper selector (the launcher and Tools have their own paths).
// Works whether the program ignores SIGCHLD (the shell) or not (Settings): the end of a child is looked for with waitpid first and
// with kill(pid, 0) when waitpid has nothing to say.
#pragma once

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>

namespace clipboard {
constexpr size_t kMaxPaste = 64 * 1024;

inline bool childGone(pid_t pid) {
    int st = 0;
    const pid_t r = waitpid(pid, &st, WNOHANG);
    if (r == pid) return true;
    if (r < 0) return errno == ECHILD ? kill(pid, 0) != 0 : true;  // SIGCHLD ignored: the child is gone when it no longer exists
    return false;
}

inline bool waitChild(pid_t pid, int timeoutMs) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        if (childGone(pid)) return true;
        usleep(4000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    return false;
}

inline bool copy(const std::string& text) {
    int in[2];
    if (pipe2(in, O_CLOEXEC) != 0) return false;
    const pid_t pid = fork();
    if (pid < 0) { close(in[0]); close(in[1]); return false; }
    if (pid == 0) {
        dup2(in[0], 0);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); }
        signal(SIGPIPE, SIG_DFL);
        execlp("wl-copy", "wl-copy", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(in[0]);
    signal(SIGPIPE, SIG_IGN);
    size_t done = 0;
    while (done < text.size()) {
        const ssize_t w = write(in[1], text.data() + done, text.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += static_cast<size_t>(w);
    }
    close(in[1]);
    return waitChild(pid, 2000) && done == text.size();
}

inline bool paste(std::string& out) {
    out.clear();
    int po[2];
    if (pipe2(po, O_CLOEXEC) != 0) return false;
    const pid_t pid = fork();
    if (pid < 0) { close(po[0]); close(po[1]); return false; }
    if (pid == 0) {
        dup2(po[1], 1);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 2); }
        execlp("wl-paste", "wl-paste", "--no-newline", "--type", "text", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(po[1]);
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    char buf[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
        if (left <= 0) break;
        pollfd p = {po[0], POLLIN, 0};
        const int pr = poll(&p, 1, static_cast<int>(left));
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) break;
        const ssize_t n = read(po[0], buf, sizeof buf);
        if (n <= 0) break;
        if (out.size() < kMaxPaste) out.append(buf, static_cast<size_t>(n));
    }
    close(po[0]);
    const bool ok = waitChild(pid, 500);
    if (out.size() > kMaxPaste) out.resize(kMaxPaste);
    if (!out.empty()) {
        size_t last = out.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(out[last]) & 0xC0) == 0x80) --last;
        const unsigned char lead = static_cast<unsigned char>(out[last]);
        const size_t width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 :
                             (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (width == 0 || out.size() - last < width) out.resize(last);
    }
    return ok && !out.empty();
}
}
