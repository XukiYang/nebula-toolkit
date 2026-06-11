#pragma once
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace nebula {
namespace logger {

struct CrashOptions {
    int  max_stack_depth        = 100;
    bool use_timestamp_filename = true;
};

class CrashCoreLogger {
public:
    CrashCoreLogger() : file_path_("crash_dump"), dump_in_progress_(false) {
        EnsureDirectoryExists();
        SaveOriginalHandlers();
        SetupSignalHandlers();
    }

    ~CrashCoreLogger() {
        RestoreOriginalHandlers();
    }

    CrashCoreLogger(const CrashCoreLogger&)            = delete;
    CrashCoreLogger& operator=(const CrashCoreLogger&) = delete;

    static CrashCoreLogger& getInstance() {
        static CrashCoreLogger singleton;
        return singleton;
    }

    /// @brief 一站式初始化（推荐）
    static void Init(const std::string& path, const CrashOptions& options = {}) {
        auto& inst       = getInstance();
        inst.file_path_  = path;
        inst.max_stack_depth_        = options.max_stack_depth > 0 ? options.max_stack_depth : 100;
        inst.use_timestamp_filename_ = options.use_timestamp_filename;
        inst.EnsureDirectoryExists();
    }

    // 保留旧 setter 兼容
    void SetFilePath(const std::string& path) {
        file_path_ = path;
        EnsureDirectoryExists();
    }

    void SetMaxStackDepth(int depth) {
        max_stack_depth_ = depth > 0 ? depth : 100;
    }

    void EnableTimestampFilenames(bool enable) {
        use_timestamp_filename_ = enable;
    }

private:
    void EnsureDirectoryExists() {
        size_t pos = file_path_.find_last_of('/');
        if (pos != std::string::npos) {
            std::string dir = file_path_.substr(0, pos);
            if (!dir.empty()) {
                mkdir(dir.c_str(), 0755);
            }
        }
    }

    void SaveOriginalHandlers() {
        original_handlers_[SIGSEGV] = signal(SIGSEGV, SIG_DFL);
        original_handlers_[SIGABRT] = signal(SIGABRT, SIG_DFL);
        original_handlers_[SIGFPE]  = signal(SIGFPE, SIG_DFL);
        original_handlers_[SIGILL]  = signal(SIGILL, SIG_DFL);
        original_handlers_[SIGBUS]  = signal(SIGBUS, SIG_DFL);
    }

    void SetupSignalHandlers() {
        struct sigaction sa;
        sa.sa_handler = OnCrash;
        sigemptyset(&sa.sa_mask);

        for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
            sigaddset(&sa.sa_mask, sig);
        }

        sa.sa_flags = SA_ONSTACK | SA_RESTART;

        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE, &sa, nullptr);
        sigaction(SIGILL, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
    }

    void RestoreOriginalHandlers() {
        for (const auto& handler : original_handlers_) {
            signal(handler.first, handler.second);
        }
    }

    void WriteDumpInfo(int sig) {
        if (dump_in_progress_.exchange(true)) {
            _exit(EXIT_FAILURE);
        }

        // 信号安全：用 char 数组 + snprintf
        char final_path[512];
        if (use_timestamp_filename_) {
            time_t now = time(nullptr);
            char   time_buf[64];
            strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", localtime(&now));
            snprintf(final_path, sizeof(final_path), "%s_%s.log", file_path_.c_str(), time_buf);
        } else {
            snprintf(final_path, sizeof(final_path), "%s.log", file_path_.c_str());
        }

        int fd = open(final_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            fd = STDERR_FILENO;
        }

        WriteCrashReport(fd, sig);

        if (fd != STDERR_FILENO) {
            close(fd);
        }
    }

    void WriteCrashReport(int fd, int sig) {
        // 信号安全：用 snprintf + 固定 buffer，不用 stringstream
        char buf[4096];
        int  offset = 0;

        time_t now = time(nullptr);
        char   time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

        offset += snprintf(buf + offset, sizeof(buf) - offset,
                           "=== Crash Report ===\n"
                           "Time: %s\n"
                           "Signal: %d (%s)\n"
                           "PID: %d\n"
                           "PPID: %d\n"
                           "UID: %d\n",
                           time_buf, sig, strsignal(sig), getpid(), getppid(), getuid());

        // 写入基本信息
        write(fd, buf, offset);

        // 堆栈跟踪：直接用 backtrace_symbols_fd，信号安全
        void* bt_buf[128];
        int   depth = max_stack_depth_ > 128 ? 128 : max_stack_depth_;
        int   stack_size = backtrace(bt_buf, depth);

        // 写入堆栈标题
        const char* stack_header = "\n=== Stack Trace ===\n";
        write(fd, stack_header, strlen(stack_header));

        // backtrace_symbols_fd 是 async-signal-safe 的
        backtrace_symbols_fd(bt_buf, stack_size, fd);
    }

    static void OnCrash(int sig) {
        getInstance().WriteDumpInfo(sig);

        // 恢复默认处理并重新触发信号，确保生成 core dump
        signal(sig, SIG_DFL);
        raise(sig);
    }

private:
    std::string                           file_path_;
    std::atomic<bool>                     dump_in_progress_;
    int                                   max_stack_depth_{100};
    bool                                  use_timestamp_filename_{true};
    std::unordered_map<int, sighandler_t> original_handlers_;
};

}  // namespace logger
}  // namespace nebula
