#include "platform/desktop/Subprocess.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>

extern char** environ;

namespace fvb {
namespace subprocess {

namespace {

// childReads: the pipe end the child gets as its stdin (otherwise its stdout).
// The child's other standard streams: stdin from /dev/null when it's not the
// pipe, stdout to /dev/null when it's not the pipe, stderr left as ours so its
// error messages show in the terminal.
pid_t start(const std::vector<std::string>& args, int pipeEnd, bool childReads) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (childReads) {
        posix_spawn_file_actions_adddup2(&actions, pipeEnd, STDIN_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    } else {
        posix_spawn_file_actions_adddup2(&actions, pipeEnd, STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    pid_t pid = -1;
    const int result = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    return result == 0 ? pid : -1;
}

// So no other program started meanwhile keeps a copy of either end open. The
// copy made for the child as its stdin or stdout isn't affected.
void closeOnExec(const int fds[2]) {
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
}

int waitFor(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace

int runReadingOutput(const std::vector<std::string>& args,
                     const std::function<bool(const char* data, size_t length)>& onOutput) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    closeOnExec(fds);
    const pid_t pid = start(args, fds[1], false);
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return -1;
    }

    bool stopped = false;
    char buffer[16384];
    for (;;) {
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (!onOutput(buffer, static_cast<size_t>(n))) {
            stopped = true;
            kill(pid, SIGKILL);  // straight away, without it complaining about the closed pipe
            break;
        }
    }
    close(fds[0]);
    const int code = waitFor(pid);
    return stopped ? -1 : code;
}

int runWithInput(const std::vector<std::string>& args, const char* input, size_t length) {
    // If the program quits early, writing to it would otherwise kill this one.
    signal(SIGPIPE, SIG_IGN);

    int fds[2];
    if (pipe(fds) != 0) return -1;
    closeOnExec(fds);
    const pid_t pid = start(args, fds[0], true);
    close(fds[0]);
    if (pid < 0) {
        close(fds[1]);
        return -1;
    }

    size_t sent = 0;
    while (sent < length) {
        const ssize_t n = write(fds[1], input + sent, length - sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
    close(fds[1]);
    return waitFor(pid);
}

bool isAvailable(const std::string& program) {
    return runReadingOutput({program, "-version"}, [](const char*, size_t) { return true; }) == 0;
}

}  // namespace subprocess
}  // namespace fvb
