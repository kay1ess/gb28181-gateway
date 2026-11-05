#ifndef GB28181_TESTS_CHILD_PROCESS_H_
#define GB28181_TESTS_CHILD_PROCESS_H_
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace gb28181::test {
// Test processes and all descendants are owned by a job/process group, so a
// failed media assertion cannot leave a simulator's FFmpeg child running.
class ChildProcess {
 public:
  explicit ChildProcess(const std::vector<std::string>& arguments,
                        const std::string& log_path = {}) {
    if (arguments.empty())
      throw std::runtime_error("empty process command");
#ifdef _WIN32
    std::string command;
    for (const auto& argument : arguments) {
      command += '"';
      size_t slashes = 0;
      for (char c : argument) {
        if (c == '\\') {
          ++slashes;
          continue;
        }
        if (c == '"')
          command.append(slashes * 2 + 1, '\\');
        else
          command.append(slashes, '\\');
        slashes = 0;
        command += c;
      }
      command.append(slashes * 2, '\\');
      command += "\" ";
    }
    job_ = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job_ ||
        !SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
      if (job_)
        CloseHandle(job_);
      throw std::runtime_error("cannot create child job");
    }
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);

    struct Handles {
      HANDLE output = INVALID_HANDLE_VALUE;
      HANDLE input = INVALID_HANDLE_VALUE;

      ~Handles() {
        if (output != INVALID_HANDLE_VALUE)
          CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
          CloseHandle(input);
      }
    } handles;

    if (!log_path.empty()) {
      SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr,
                                     TRUE};
      handles.output = CreateFileA(log_path.c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ, &attributes, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
      handles.input = CreateFileA(
          "NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handles.output == INVALID_HANDLE_VALUE ||
          handles.input == INVALID_HANDLE_VALUE) {
        CloseHandle(job_);
        job_ = nullptr;
        throw std::runtime_error("cannot open child log");
      }
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdOutput = startup.hStdError = handles.output;
      startup.hStdInput = handles.input;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr,
                        !log_path.empty(), CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
      CloseHandle(job_);
      job_ = nullptr;
      throw std::runtime_error("cannot start child process: " + arguments[0]);
    }
    if (!AssignProcessToJobObject(job_, process.hProcess)) {
      TerminateProcess(process.hProcess, 1);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      CloseHandle(job_);
      job_ = nullptr;
      throw std::runtime_error("cannot own child process tree");
    }
    process_ = process.hProcess;
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
#else
    std::vector<char*> argv;
    for (const auto& argument : arguments)
      argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_ = fork();
    if (pid_ < 0)
      throw std::runtime_error("fork failed");
    if (pid_ == 0) {
      setpgid(0, 0);
      if (!log_path.empty()) {
        const int log =
            open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (log < 0)
          _exit(126);
        dup2(log, STDOUT_FILENO);
        dup2(log, STDERR_FILENO);
        close(log);
      }
      execvp(argv[0], argv.data());
      _exit(127);
    }
    setpgid(pid_, pid_);
#endif
  }

  ~ChildProcess() {
#ifdef _WIN32
    if (job_)
      CloseHandle(job_);
    if (process_) {
      WaitForSingleObject(process_, 5000);
      CloseHandle(process_);
    }
#else
    if (pid_ > 0) {
      kill(-pid_, SIGTERM);
      if (Wait(std::chrono::milliseconds(500)) == -1) {
        kill(-pid_, SIGKILL);
        int status;
        waitpid(pid_, &status, 0);
      }
    }
#endif
  }

  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;

  int Wait(std::chrono::milliseconds timeout) {
#ifdef _WIN32
    if (WaitForSingleObject(process_, static_cast<DWORD>(timeout.count())) !=
        WAIT_OBJECT_0)
      return -1;
    DWORD code = 1;
    GetExitCodeProcess(process_, &code);
    return static_cast<int>(code);
#else
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      int status = 0;
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return -1;
#endif
  }

 private:
#ifdef _WIN32
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
#else
  pid_t pid_ = -1;
#endif
};
}  // namespace gb28181::test
#endif  // GB28181_TESTS_CHILD_PROCESS_H_
