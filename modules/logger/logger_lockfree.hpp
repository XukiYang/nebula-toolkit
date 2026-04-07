#pragma once
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "../containers/lockfree_queue.hpp"
#include "./config_handler/ini_reader.hpp"
namespace nebula {
namespace configs {
struct LogGlobal {
    size_t      max_file_size = 1024 * 1024;  // 1MB
    bool        print_line    = false;
    bool        print_func    = false;
    bool        print_time    = false;
    std::string log_directory;
};

struct LogAsync {
    size_t ring_buffer_size_kb = 64 * 1024;
    size_t batch_size_kb       = 4 * 1024;
    size_t max_flush_size      = 64 * 1024;
};

struct LogLevel {
    bool msg   = false;
    bool info  = false;
    bool warn  = false;
    bool debug = false;
    bool error = false;
};
};  // namespace configs

struct FileManager {
    std::ofstream file;
    std::string   current_date;
    size_t        current_index = 0;
};

class Logger {
public:
    enum LogLevel { MSG, INFO, WARN, DEBUG, ERROR };

private:
    static constexpr const char *CONFIG_PATH    = "./configs/log_config.ini";
    static constexpr const char *GLOBAL_SECTION = "LOG_GLOBAL";
    static constexpr const char *ASYNC_SECTION  = "LOG_ASYNC";  // 修复：原为 LOG_GLOBAL
    static constexpr const char *LEVEL_SECTION  = "LOG_LEVEL";

    FileManager file_manager_;

    configs::LogGlobal log_global_config_;
    configs::LogAsync  log_async_config_;
    configs::LogLevel  log_level_config_;

    std::unique_ptr<config_handler::IniReader> ini_reader_;

    std::unique_ptr<containers::LockFreeQueue<char>> buffer_;

    std::unique_ptr<std::thread> cust_thread_;
    std::atomic<bool>            cust_thread_running_{true};

    inline void LoadConfigOnce() {
        // LOG_GLOBAL
        ini_reader_->GetValue(GLOBAL_SECTION, "max_file_size_kb", log_global_config_.max_file_size);
        log_global_config_.max_file_size *= 1024;  // KB to bytes
        ini_reader_->GetValue(GLOBAL_SECTION, "print_line", log_global_config_.print_line);
        ini_reader_->GetValue(GLOBAL_SECTION, "print_func", log_global_config_.print_func);
        ini_reader_->GetValue(GLOBAL_SECTION, "print_time", log_global_config_.print_time);
        ini_reader_->GetValue(GLOBAL_SECTION, "log_directory", log_global_config_.log_directory);

        // LOG_ASYNC
        ini_reader_->GetValue(ASYNC_SECTION, "ring_buffer_size_kb", log_async_config_.ring_buffer_size_kb);
        ini_reader_->GetValue(ASYNC_SECTION, "batch_size_kb", log_async_config_.batch_size_kb);
        ini_reader_->GetValue(ASYNC_SECTION, "max_flush_size", log_async_config_.max_flush_size);

        // LOG_LEVEL
        ini_reader_->GetValue(LEVEL_SECTION, "msg", log_level_config_.msg);
        ini_reader_->GetValue(LEVEL_SECTION, "info", log_level_config_.info);
        ini_reader_->GetValue(LEVEL_SECTION, "warn", log_level_config_.warn);
        ini_reader_->GetValue(LEVEL_SECTION, "debug", log_level_config_.debug);
        ini_reader_->GetValue(LEVEL_SECTION, "error", log_level_config_.error);

        fmt::print(
            "------LOG_GLOBAL CONFIG------\n"
            "max_file_size:{}, print_line:{}, print_func:{}, print_time:{}, log_directory:{}\n",
            log_global_config_.max_file_size, log_global_config_.print_line, log_global_config_.print_func,
            log_global_config_.print_time, log_global_config_.log_directory);

        fmt::print(
            "------LOG_ASYNC CONFIG------\n"
            "ring_buffer_size_kb:{}, batch_size_kb:{}, max_flush_size:{}\n",
            log_async_config_.ring_buffer_size_kb, log_async_config_.batch_size_kb, log_async_config_.max_flush_size);

        fmt::print(
            "------LOG_LEVEL CONFIG------\n"
            "msg:{}, info:{}, warn:{}, debug:{}, error:{}\n",
            log_level_config_.msg, log_level_config_.info, log_level_config_.warn, log_level_config_.debug,
            log_level_config_.error);
    }

    inline bool ShouldLog(const LogLevel &level) const {
        switch (level) {
        case MSG:
            return log_level_config_.msg;
        case INFO:
            return log_level_config_.info;
        case WARN:
            return log_level_config_.warn;
        case DEBUG:
            return log_level_config_.debug;
        case ERROR:
            return log_level_config_.error;
        default:
            return false;
        }
    }

    inline const char *LevelToString(const LogLevel &level) const {
        static const char *levels[] = {"[MSG] ", "[INFO] ", "[WARN] ", "[DEBUG] ", "[ERROR] "};
        return levels[level];
    }

    inline std::string CurrentTime() const {
        auto               now  = std::chrono::system_clock::now();
        auto               time = std::chrono::system_clock::to_time_t(now);
        std::tm            tm   = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    inline std::string CurrentDate() const {
        auto               now  = std::chrono::system_clock::now();
        auto               time = std::chrono::system_clock::to_time_t(now);
        std::tm            tm   = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }

    inline void RotateFileIfNeeded() {
        std::string date = CurrentDate();

        if (date != file_manager_.current_date) {
            file_manager_.current_date  = date;
            file_manager_.current_index = 0;
            OpenNewFile();
        } else if (file_manager_.file.tellp() > static_cast<std::streampos>(log_global_config_.max_file_size)) {
            file_manager_.current_index++;
            OpenNewFile();
        }
    }

    inline void OpenNewFile() {
        if (file_manager_.file.is_open()) {
            file_manager_.file.close();
        }

        std::string filename = log_global_config_.log_directory + '/' + file_manager_.current_date + "_"
                               + std::to_string(file_manager_.current_index) + ".log";
        file_manager_.file.open(filename, std::ios::app);
        if (!file_manager_.file.is_open()) {
            std::cerr << "Logger: Cannot open log file: " << filename << std::endl;
        }
    }

    void CustThreadProc() {
        size_t            batch_size = log_async_config_.batch_size_kb;
        std::vector<char> read_buffer(batch_size);

        while (cust_thread_running_.load(std::memory_order_relaxed)) {
            if (buffer_->Empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            size_t available = buffer_->AvaToRead();
            size_t to_read   = std::min(available, batch_size);

            if (to_read > 0) {
                buffer_->PopBulk(read_buffer.data(), to_read);
                RotateFileIfNeeded();
                file_manager_.file.write(read_buffer.data(), static_cast<std::streamsize>(to_read));
                file_manager_.file.flush();
            }

            if (batch_size != log_async_config_.batch_size_kb) {
                batch_size = log_async_config_.batch_size_kb;
                read_buffer.resize(batch_size);
            }
        }

        size_t remaining = buffer_->AvaToRead();
        if (remaining > 0) {
            std::vector<char> final_buf(remaining);
            buffer_->PopBulk(final_buf.data(), remaining);
            RotateFileIfNeeded();
            file_manager_.file.write(final_buf.data(), static_cast<std::streamsize>(remaining));
        }
        file_manager_.file.flush();
        if (file_manager_.file.is_open()) {
            file_manager_.file.close();
        }
    }

public:
    Logger()
        : ini_reader_(std::make_unique<config_handler::IniReader>(CONFIG_PATH)),
          buffer_(std::make_unique<containers::LockFreeQueue<char>>(log_async_config_.ring_buffer_size_kb)) {
        LoadConfigOnce();
        OpenNewFile();
        cust_thread_ = std::make_unique<std::thread>(&Logger::CustThreadProc, this);
    }

    ~Logger() {
        cust_thread_running_.store(false, std::memory_order_release);
        if (cust_thread_ && cust_thread_->joinable()) {
            cust_thread_->join();
        }
    }

    template <typename... Args>
    void LogCout(LogLevel level, const char *func, size_t line, Args &&... args) {
        if (!ShouldLog(level)) return;

        std::ostringstream oss;
        if (log_global_config_.print_time) oss << CurrentTime() << " " << LevelToString(level);
        if (log_global_config_.print_func) oss << "[" << func << " ";
        if (log_global_config_.print_line) oss << "L" << line << "] ";
        ((oss << std::forward<Args>(args)), ...) << "\n";

        std::cout << oss.str();

        if (level != MSG) {
            std::string log_msg = oss.str();
            buffer_->PushBulk(log_msg.c_str(), log_msg.size());
        }
    }

    void LogPrint(LogLevel level, const char *func, size_t line, const char *format, ...) {
        if (!ShouldLog(level)) return;

        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        std::ostringstream oss;
        if (log_global_config_.print_time) oss << CurrentTime() << " " << LevelToString(level);
        if (log_global_config_.print_func) oss << "[" << func << " ";
        if (log_global_config_.print_line) oss << "L" << line << "] ";
        oss << buffer << "\n";

        std::cout << oss.str();

        if (level != MSG) {
            std::string log_msg = oss.str();
            buffer_->PushBulk(log_msg.c_str(), log_msg.size());
        }
    }

    template <typename... Args>
    void LogFmt(LogLevel level, const char *func, size_t line, fmt::format_string<Args...> format, Args &&... args) {
        if (!ShouldLog(level)) return;

        std::string        log_str = fmt::format(format, std::forward<Args>(args)...);
        std::ostringstream oss;
        if (log_global_config_.print_time) oss << CurrentTime() << " " << LevelToString(level);
        if (log_global_config_.print_func) oss << "[" << func << " ";
        if (log_global_config_.print_line) oss << "L" << line << "] ";
        oss << log_str << "\n";

        std::cout << oss.str();

        if (level != MSG) {
            std::string log_msg = oss.str();
            buffer_->PushBulk(log_msg.c_str(), log_msg.size());
        }
    }

    template <typename T>
    void LogVector(LogLevel level, const char *func, size_t line, const std::vector<T> &vector) {
        if (!ShouldLog(level)) return;

        std::ostringstream oss;
        if (log_global_config_.print_time) oss << CurrentTime() << " " << LevelToString(level);
        if (log_global_config_.print_func) oss << "[" << func << " ";
        if (log_global_config_.print_line) oss << "L" << line << "] ";

        for (size_t i = 0; i < vector.size(); ++i) {
            if (i != 0) oss << ",";
            if constexpr (std::is_same_v<T, char> || std::is_same_v<T, unsigned char>) {
                oss << static_cast<int>(vector[i]);
            } else {
                oss << vector[i];
            }
        }
        oss << "\n";

        std::cout << oss.str();
    }

    static Logger &Instance() {
        static Logger instance;
        return instance;
    }
};

#define LOG_MSG(...)   Logger::Instance().LogCout(Logger::MSG, __func__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  Logger::Instance().LogCout(Logger::INFO, __func__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  Logger::Instance().LogCout(Logger::WARN, __func__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::Instance().LogCout(Logger::DEBUG, __func__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger::Instance().LogCout(Logger::ERROR, __func__, __LINE__, __VA_ARGS__)

#define LOGP_MSG(fmt, ...)   Logger::Instance().LogPrint(Logger::MSG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_INFO(fmt, ...)  Logger::Instance().LogPrint(Logger::INFO, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_WARN(fmt, ...)  Logger::Instance().LogPrint(Logger::WARN, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_DEBUG(fmt, ...) Logger::Instance().LogPrint(Logger::DEBUG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_ERROR(fmt, ...) Logger::Instance().LogPrint(Logger::ERROR, __func__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_VECTOR(vector) Logger::Instance().LogVector(Logger::MSG, __func__, __LINE__, vector)

#define LOGF_MSG(fmt, ...)   Logger::Instance().LogFmt(Logger::MSG, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_INFO(fmt, ...)  Logger::Instance().LogFmt(Logger::INFO, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_WARN(fmt, ...)  Logger::Instance().LogFmt(Logger::WARN, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_DEBUG(fmt, ...) Logger::Instance().LogFmt(Logger::DEBUG, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_ERROR(fmt, ...) Logger::Instance().LogFmt(Logger::ERROR, __func__, __LINE__, fmt, __VA_ARGS__)

}  // namespace nebula