// Running programs from a window of the shell: always with an argument list, never through a shell. Included once by a program, inside its anonymous namespace
// (after <chrono>, <poll.h>, <signal.h>, <sys/wait.h> and `using Clock`).
#pragma once

// ---- running programs (a file dialog, the clipboard): always with an argument list, never through a shell ----------------------------------
// Runs a program and returns what it printed. `timeoutMs` > 0 gives up (and kills it) after that long. `ok` says it exited with 0.
std::string runCapture(const std::vector<std::string>& argv, int timeoutMs, bool& ok) {
    ok = false;
    int out[2];
    if (pipe(out) != 0) return "";
    const pid_t pid = fork();
    if (pid < 0) { close(out[0]); close(out[1]); return ""; }
    if (pid == 0) {
        dup2(out[1], 1);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 2); }
        close(out[0]);
        close(out[1]);
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(out[1]);
    std::string result;
    const auto began = Clock::now();
    char buf[4096];
    while (true) {
        pollfd p{out[0], POLLIN, 0};
        int wait = -1;
        if (timeoutMs > 0) {
            const int left = timeoutMs - static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count());
            if (left <= 0) { kill(pid, SIGKILL); break; }
            wait = left;
        }
        const int r = poll(&p, 1, wait);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { if (timeoutMs > 0) kill(pid, SIGKILL); break; }
        const ssize_t n = read(out[0], buf, sizeof buf);
        if (n <= 0) break;
        result.append(buf, static_cast<size_t>(n));
        if (result.size() > 64u * 1024 * 1024) { kill(pid, SIGKILL); break; }
    }
    close(out[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return result;
}
// Hands text to a program on its input (wl-copy), and does not wait for what it leaves running (it keeps the clipboard).
bool runWrite(const std::vector<std::string>& argv, const std::string& input) {
    int in[2];
    if (pipe(in) != 0) return false;
    const pid_t pid = fork();
    if (pid < 0) { close(in[0]); close(in[1]); return false; }
    if (pid == 0) {
        dup2(in[0], 0);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); }
        close(in[0]);
        close(in[1]);
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(in[0]);
    signal(SIGPIPE, SIG_IGN);
    for (size_t done = 0; done < input.size();) {
        const ssize_t n = write(in[1], input.data() + done, input.size() - done);
        if (n <= 0) { if (errno == EINTR) continue; break; }
        done += static_cast<size_t>(n);
    }
    close(in[1]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
