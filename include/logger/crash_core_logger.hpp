#pragma once
#include <cxxabi.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nebula {
namespace logger {
class CrashCoreLogger {
public:
    CrashCoreLogger() : file_path_("crash_dump"), dump_in_progress_(false) {
        // 确保日志目录存在
        EnsureDirectoryExists();
        // 保存原始信号处理器
        SaveOriginalHandlers();
        // 设置信号处理器
        SetupSignalHandlers();
        std::cout << "CrashCoreLogger initialized" << std::endl;
    }

    ~CrashCoreLogger() {
        RestoreOriginalHandlers();
    }

public:
    CrashCoreLogger(const CrashCoreLogger&) = delete;
    CrashCoreLogger& operator=(const CrashCoreLogger&) = delete;

public:
    static CrashCoreLogger& getInstance() {
        static CrashCoreLogger singleton;
        return singleton;
    }

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
                mkdir(dir.c_str(), 0755);  // 尝试创建目录，忽略错误
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

        // 阻塞其他信号，防止嵌套崩溃
        for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
            sigaddset(&sa.sa_mask, sig);
        }

        sa.sa_flags = SA_ONSTACK | SA_RESTART;  // 使用备用栈

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

    std::string DemangleSymbol(const char* symbol) {
        if (!symbol) return "??";

        std::string result = symbol;
        size_t      start  = result.find('(');
        size_t      end    = result.find('+', start);

        if (start != std::string::npos && end != std::string::npos) {
            std::string mangled   = result.substr(start + 1, end - start - 1);
            int         status    = 0;
            char*       demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);

            if (demangled && status == 0) {
                result.replace(start + 1, end - start - 1, demangled);
                free(demangled);
            }
        }

        return result;
    }

    void WriteDumpInfo(int sig) {
        if (dump_in_progress_.exchange(true)) {
            // 已经在处理崩溃，避免递归
            _exit(EXIT_FAILURE);
        }

        std::string final_path = file_path_;
        if (use_timestamp_filename_) {
            time_t now = time(nullptr);
            char   time_buf[64];
            strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", localtime(&now));
            final_path += "_" + std::string(time_buf) + ".log";
        }

        // 使用低级I/O确保异步信号安全
        int fd = open(final_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            // 回退到标准错误
            fd = STDERR_FILENO;
        }

        WriteCrashReport(fd, sig);

        if (fd != STDERR_FILENO) {
            close(fd);
        }
    }

    void WriteCrashReport(int fd, int sig) {
        std::stringstream ss;

        // 基本信息
        time_t now = time(nullptr);
        char   time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

        ss << "=== 程序崩溃报告 ===\n";
        ss << "时间: " << time_buf << "\n";
        ss << "信号: " << sig << " (" << strsignal(sig) << ")\n";
        ss << "PID: " << getpid() << "\n";
        ss << "PPID: " << getppid() << "\n";
        ss << "UID: " << getuid() << "\n";

        // 堆栈跟踪
        ss << "\n=== 堆栈跟踪 ===\n";

        std::vector<void*> buffer(max_stack_depth_);
        int                stack_size = backtrace(buffer.data(), max_stack_depth_);
        char**             symbols    = backtrace_symbols(buffer.data(), stack_size);

        if (symbols) {
            for (int i = 0; i < stack_size; ++i) {
                ss << "[" << std::setw(2) << i << "] ";
                ss << DemangleSymbol(symbols[i]) << "\n";
            }
            free(symbols);
        } else {
            ss << "无法获取堆栈符号\n";
            backtrace_symbols_fd(buffer.data(), stack_size, fd);
        }

        // 写入文件
        std::string report = ss.str();
        write(fd, report.c_str(), report.length());
    }

    static void OnCrash(int sig) {
        getInstance().WriteDumpInfo(sig);

        // 恢复默认处理并重新触发信号，确保生成core dump
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