#pragma once

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
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "config_handler/ini_reader.hpp"
#include "containers/circular_buffer.hpp"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef ERROR
#undef ERROR
#endif

namespace nebula {
namespace configs {
struct LogGlobal {
    size_t      max_file_size = 1024 * 1024;
    bool        print_line    = false;
    bool        print_func    = false;
    bool        print_time    = false;
    std::string log_directory = "./logs";
};

struct LogAsync {
    size_t ring_buffer_size_kb = 64 * 1024;
    size_t batch_size_kb       = 4 * 1024;
    size_t max_flush_size      = 64 * 1024;
};

struct LogLevel {
    bool msg   = true;
    bool info  = true;
    bool warn  = true;
    bool debug = true;
    bool error = true;
};
}  // namespace configs

namespace logger {

struct FileManager {
    std::ofstream file;
    std::string   current_date;
    size_t        current_index = 0;
};

class Logger {
public:
    enum LogLevel { MSG, INFO, WARN, DEBUG, ERROR };

private:
    static constexpr const char* CONFIG_PATH    = "./configs/log_config.ini";
    static constexpr const char* GLOBAL_SECTION = "LOG_GLOBAL";
    static constexpr const char* ASYNC_SECTION  = "LOG_GLOBAL";
    static constexpr const char* LEVEL_SECTION  = "LOG_LEVEL";

    std::mutex              mutex_;
    FileManager             file_manager_;
    configs::LogGlobal      log_global_config_;
    configs::LogAsync       log_async_config_;
    configs::LogLevel       log_level_config_;
    std::unique_ptr<config_handler::IniReader> ini_reader_;

    std::atomic<bool>            monitor_config_thread_running_{true};
    std::unique_ptr<std::thread> config_monitor_;

    std::mutex                              cv_mutex_;
    std::condition_variable                 cv_;
    std::unique_ptr<containers::CircularBuffer> ring_buffer_;
    std::unique_ptr<std::thread>            cust_thread_;
    bool                                    has_pending_data_{false};
    std::atomic<bool>                       cust_thread_running_{true};

private:
    void UpdateConfig() {
        std::lock_guard<std::mutex> lock(mutex_);
        ini_reader_->GetValue(GLOBAL_SECTION, "max_file_size_kb", log_global_config_.max_file_size);
        log_global_config_.max_file_size *= 1024;
        ini_reader_->GetValue(GLOBAL_SECTION, "print_line", log_global_config_.print_line);
        ini_reader_->GetValue(GLOBAL_SECTION, "print_func", log_global_config_.print_func);
        ini_reader_->GetValue(GLOBAL_SECTION, "print_time", log_global_config_.print_time);
        ini_reader_->GetValue(GLOBAL_SECTION, "log_directory", log_global_config_.log_directory);

        ini_reader_->GetValue(ASYNC_SECTION, "ring_buffer_size_kb", log_async_config_.ring_buffer_size_kb);
        ini_reader_->GetValue(ASYNC_SECTION, "batch_size_kb", log_async_config_.batch_size_kb);
        ini_reader_->GetValue(ASYNC_SECTION, "max_flush_size", log_async_config_.max_flush_size);

        ini_reader_->GetValue(LEVEL_SECTION, "msg", log_level_config_.msg);
        ini_reader_->GetValue(LEVEL_SECTION, "info", log_level_config_.info);
        ini_reader_->GetValue(LEVEL_SECTION, "warn", log_level_config_.warn);
        ini_reader_->GetValue(LEVEL_SECTION, "debug", log_level_config_.debug);
        ini_reader_->GetValue(LEVEL_SECTION, "error", log_level_config_.error);
    }

    void MonitorConfigChanges() {
        time_t last_mod = 0;
        while (monitor_config_thread_running_.load()) {
            struct stat file_stat;
            if (stat(CONFIG_PATH, &file_stat) == 0) {
                if (file_stat.st_mtime != last_mod) {
                    last_mod = file_stat.st_mtime;
                    UpdateConfig();
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    bool ShouldLog(const LogLevel& level) const {
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

    const char* LevelToString(const LogLevel& level) const {
        static const char* kLevels[] = {"[MSG] ", "[INFO] ", "[WARN] ", "[DEBUG] ", "[ERROR] "};
        return kLevels[level];
    }

    std::string CurrentTime() const {
        auto    now  = std::chrono::system_clock::now();
        auto    time = std::chrono::system_clock::to_time_t(now);
        std::tm tm   = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::string CurrentDate() const {
        auto    now  = std::chrono::system_clock::now();
        auto    time = std::chrono::system_clock::to_time_t(now);
        std::tm tm   = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }

    void OpenNewFile() {
        if (file_manager_.file.is_open()) {
            file_manager_.file.close();
        }
        const std::string filename = log_global_config_.log_directory + "/" + file_manager_.current_date + "_"
                                     + std::to_string(file_manager_.current_index) + ".log";
        file_manager_.file.open(filename, std::ios::app);
    }

    void RotateFileIfNeeded() {
        const std::string date = CurrentDate();
        if (date != file_manager_.current_date) {
            file_manager_.current_date  = date;
            file_manager_.current_index = 0;
            OpenNewFile();
        } else if (file_manager_.file.tellp() > static_cast<std::streamoff>(log_global_config_.max_file_size)) {
            ++file_manager_.current_index;
            OpenNewFile();
        }
    }

    void NotifyConsumer() {
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
            has_pending_data_ = true;
        }
        cv_.notify_one();
    }

    void CustThreadProc() {
        size_t               batch_size = log_async_config_.batch_size_kb;
        std::vector<uint8_t> read_buffer(batch_size);
        while (true) {
            {
                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return !cust_thread_running_.load() || has_pending_data_;
                });
                has_pending_data_ = false;
            }

            while (true) {
                const size_t available_to_read = ring_buffer_->AvailableToRead();
                const size_t min_read_bytes    = std::min(available_to_read, log_async_config_.batch_size_kb);
                if (min_read_bytes == 0) {
                    break;
                }
                if (read_buffer.size() < min_read_bytes) {
                    read_buffer.resize(min_read_bytes);
                }
                ring_buffer_->Read(read_buffer, min_read_bytes);
                RotateFileIfNeeded();
                if (file_manager_.file.is_open()) {
                    file_manager_.file.write(reinterpret_cast<const char*>(read_buffer.data()), min_read_bytes);
                    if (min_read_bytes >= log_async_config_.max_flush_size) {
                        file_manager_.file.flush();
                    }
                }
            }

            if (!cust_thread_running_.load() && ring_buffer_->IsEmpty()) {
                break;
            }
        }

        if (file_manager_.file.is_open()) {
            file_manager_.file.flush();
        }
    }

public:
    Logger()
        : ini_reader_(std::make_unique<config_handler::IniReader>(CONFIG_PATH)),
          ring_buffer_(std::make_unique<containers::CircularBuffer>(log_async_config_.ring_buffer_size_kb)) {
        UpdateConfig();
        config_monitor_ = std::make_unique<std::thread>(&Logger::MonitorConfigChanges, this);
        cust_thread_    = std::make_unique<std::thread>(&Logger::CustThreadProc, this);
    }

    ~Logger() {
        monitor_config_thread_running_.store(false);
        cust_thread_running_.store(false);
        cv_.notify_all();

        if (config_monitor_ && config_monitor_->joinable()) {
            config_monitor_->join();
        }
        if (cust_thread_ && cust_thread_->joinable()) {
            cust_thread_->join();
        }
        if (file_manager_.file.is_open()) {
            file_manager_.file.close();
        }
    }

    template <typename... Args>
    void LogCout(LogLevel level, const char* func, size_t line, Args&&... args) {
        if (!ShouldLog(level)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream          oss;
        if (log_global_config_.print_time) {
            oss << CurrentTime() << " " << LevelToString(level);
        }
        if (log_global_config_.print_func) {
            oss << "[" << func << " ";
        }
        if (log_global_config_.print_line) {
            oss << "L" << line << "] ";
        }
        ((oss << std::forward<Args>(args)), ...) << "\n";

        const std::string log_line = oss.str();
        std::cout << log_line;
        if (level != MSG) {
            ring_buffer_->Write(log_line.data(), log_line.size());
            NotifyConsumer();
        }
    }

    void LogPrint(LogLevel level, const char* func, size_t line, const char* format, ...) {
        if (!ShouldLog(level)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        va_list                     args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        std::ostringstream oss;
        if (log_global_config_.print_time) {
            oss << CurrentTime() << " " << LevelToString(level);
        }
        if (log_global_config_.print_func) {
            oss << "[" << func << " ";
        }
        if (log_global_config_.print_line) {
            oss << "L" << line << "] ";
        }
        oss << buffer << "\n";

        const std::string log_line = oss.str();
        std::cout << log_line;
        if (level != MSG) {
            ring_buffer_->Write(log_line.data(), log_line.size());
            NotifyConsumer();
        }
    }

    template <typename... Args>
    void LogFmt(LogLevel level, const char* func, size_t line, fmt::format_string<Args...> format, Args&&... args) {
        if (!ShouldLog(level)) {
            return;
        }

        std::ostringstream oss;
        if (log_global_config_.print_time) {
            oss << CurrentTime() << " " << LevelToString(level);
        }
        if (log_global_config_.print_func) {
            oss << "[" << func << " ";
        }
        if (log_global_config_.print_line) {
            oss << "L" << line << "] ";
        }
        oss << fmt::format(format, std::forward<Args>(args)...) << "\n";

        const std::string log_line = oss.str();
        std::cout << log_line;
        if (level != MSG) {
            ring_buffer_->Write(log_line.data(), log_line.size());
            NotifyConsumer();
        }
    }

    template <typename T>
    void LogVector(LogLevel level, const char* func, size_t line, const std::vector<T>& vector) {
        if (!ShouldLog(level)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream          oss;
        if (log_global_config_.print_time) {
            oss << CurrentTime() << " " << LevelToString(level);
        }
        if (log_global_config_.print_func) {
            oss << "[" << func << " ";
        }
        if (log_global_config_.print_line) {
            oss << "L" << line << "] ";
        }
        for (size_t i = 0; i < vector.size(); ++i) {
            if (i != 0) {
                oss << ",";
            }
            if constexpr (std::is_same_v<T, unsigned char> || std::is_same_v<T, uint8_t>) {
                oss << static_cast<int>(vector[i]);
            } else {
                oss << vector[i];
            }
        }
        oss << "\n";

        const std::string log_line = oss.str();
        std::cout << log_line;
        if (level != MSG) {
            ring_buffer_->Write(log_line.data(), log_line.size());
            NotifyConsumer();
        }
    }

    static Logger& Instance() {
        static Logger instance;
        return instance;
    }
};

#define LOG_MSG(...)   nebula::logger::Logger::Instance().LogCout(nebula::logger::Logger::MSG, __func__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  nebula::logger::Logger::Instance().LogCout(nebula::logger::Logger::INFO, __func__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  nebula::logger::Logger::Instance().LogCout(nebula::logger::Logger::WARN, __func__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) nebula::logger::Logger::Instance().LogCout(nebula::logger::Logger::DEBUG, __func__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) nebula::logger::Logger::Instance().LogCout(nebula::logger::Logger::ERROR, __func__, __LINE__, __VA_ARGS__)

#define LOGP_MSG(fmt, ...)   nebula::logger::Logger::Instance().LogPrint(nebula::logger::Logger::MSG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_INFO(fmt, ...)  nebula::logger::Logger::Instance().LogPrint(nebula::logger::Logger::INFO, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_WARN(fmt, ...)  nebula::logger::Logger::Instance().LogPrint(nebula::logger::Logger::WARN, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_DEBUG(fmt, ...) nebula::logger::Logger::Instance().LogPrint(nebula::logger::Logger::DEBUG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGP_ERROR(fmt, ...) nebula::logger::Logger::Instance().LogPrint(nebula::logger::Logger::ERROR, __func__, __LINE__, fmt, ##__VA_ARGS__)

#define LOGMSG_VECTOR(vector) nebula::logger::Logger::Instance().LogVector(nebula::logger::Logger::MSG, __func__, __LINE__, vector)

#ifndef LOG_VECTOR
#define LOG_VECTOR(vector) LOGMSG_VECTOR(vector)
#endif

#define LOGF_MSG(fmt, ...)   nebula::logger::Logger::Instance().LogFmt(nebula::logger::Logger::MSG, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_INFO(fmt, ...)  nebula::logger::Logger::Instance().LogFmt(nebula::logger::Logger::INFO, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_WARN(fmt, ...)  nebula::logger::Logger::Instance().LogFmt(nebula::logger::Logger::WARN, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_DEBUG(fmt, ...) nebula::logger::Logger::Instance().LogFmt(nebula::logger::Logger::DEBUG, __func__, __LINE__, fmt, __VA_ARGS__)
#define LOGF_ERROR(fmt, ...) nebula::logger::Logger::Instance().LogFmt(nebula::logger::Logger::ERROR, __func__, __LINE__, fmt, __VA_ARGS__)

}  // namespace logger
}  // namespace nebula
