#pragma once
#include "../containers/ring_buffer.hpp"
#include "./ini_reader.hpp"
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
#include <sys/stat.h>
#include <thread>
#include <vector>

class Logger {
public:
  enum LogLevel { MSG, INFO, WARN, DEBUG, ERROR };

private:
  static constexpr const char *CONFIG_PATH = "./configs/log_config.ini";
  static constexpr const char *GLOBAL_SECTION = "LOG_GLOBAL";
  static constexpr const char *LEVEL_SECTION = "LOG_LEVEL";
  static constexpr const uint64_t RING_BUFFER_SIZE = 1024 * 64; // 64kb

  struct Config {
    size_t max_file_size = 1024 * 1024; // 1MB
    bool print_line = false;
    bool print_func = false;
    bool print_time = false;
    std::string log_directory;
    bool level_msg = false;
    bool level_info = false;
    bool level_warn = false;
    bool level_debug = false;
    bool level_error = false;
  };

  struct FileManager {
    std::ofstream file;
    std::string current_date;
    size_t current_index = 0;
  };

  std::mutex mutex_;
  FileManager file_manager_;

  Config config_;
  std::unique_ptr<IniReader> ini_reader_;

  std::atomic<bool> monitor_config_thread_running_{true};
  std::unique_ptr<std::thread> config_monitor_;

  std::mutex ring_buffer_mutex_;
  std::condition_variable cv;
  std::unique_ptr<containers::RingBuffer> ring_buffer_; // async buffer
  std::unique_ptr<std::thread> cust_thread_;

  std::atomic<bool> cust_thread_running_{true};

  void UpdateConfig() {
    std::lock_guard<std::mutex> lock(mutex_);

    ini_reader_->GetValue(GLOBAL_SECTION, "max_file_size_kb",
                          config_.max_file_size);
    config_.max_file_size *= 1024; // KB to bytes

    ini_reader_->GetValue(GLOBAL_SECTION, "print_line", config_.print_line);
    ini_reader_->GetValue(GLOBAL_SECTION, "print_func", config_.print_func);
    ini_reader_->GetValue(GLOBAL_SECTION, "print_time", config_.print_time);
    ini_reader_->GetValue(GLOBAL_SECTION, "log_directory",
                          config_.log_directory);

    ini_reader_->GetValue(LEVEL_SECTION, "msg", config_.level_msg);
    ini_reader_->GetValue(LEVEL_SECTION, "info", config_.level_info);
    ini_reader_->GetValue(LEVEL_SECTION, "warn", config_.level_warn);
    ini_reader_->GetValue(LEVEL_SECTION, "debug", config_.level_debug);
    ini_reader_->GetValue(LEVEL_SECTION, "error", config_.level_error);
  }

  void MonitorConfigChanges() {
    time_t last_mod = 0;
    while (monitor_config_thread_running_) {
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

  bool ShouldLog(LogLevel level) const {
    switch (level) {
    case MSG:
      return config_.level_msg;
    case INFO:
      return config_.level_info;
    case WARN:
      return config_.level_warn;
    case DEBUG:
      return config_.level_debug;
    case ERROR:
      return config_.level_error;
    default:
      return false;
    }
  }

  const char *LevelToString(LogLevel level) const {
    static const char *levels[] = {"[MSG] ", "[INFO] ", "[WARN] ", "[DEBUG] ",
                                   "[ERROR] "};
    return levels[level];
  }

  std::string CurrentTime() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
  }

  std::string CurrentDate() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
  }

  void RotateFileIfNeeded() {
    std::string date = CurrentDate();

    if (date != file_manager_.current_date) {
      file_manager_.current_date = date;
      file_manager_.current_index = 0;
      OpenNewFile();
    } else if (file_manager_.file.tellp() > config_.max_file_size) {
      file_manager_.current_index++;
      OpenNewFile();
    }
  }

  void OpenNewFile() {
    if (file_manager_.file.is_open()) {
      file_manager_.file.close();
    }

    std::string filename = config_.log_directory + '/' +
                           file_manager_.current_date + "_" +
                           std::to_string(file_manager_.current_index) + ".log";
    file_manager_.file.open(filename, std::ios::app);
    if (!file_manager_.file.is_open()) {
      throw std::runtime_error("Cannot open log file: " + filename);
    }
  }

  void CustThreadProc() {
    const size_t BATCH_SIZE = 4096;
    const size_t MAX_FLUSH_BYTES = 65536; // 最大flush字节
    size_t CUR_WRITE_BYTES = 0;
    std::vector<uint8_t> read_buffer(BATCH_SIZE); // 1copy 缓冲容器

    while (cust_thread_running_.load()) {
      std::unique_lock<std::mutex> lock(ring_buffer_mutex_);

      if (cv.wait_for(lock, std::chrono::milliseconds(100),
                      [&] { return !ring_buffer_->IsEmpty(); })) {
        // 限制最大块字节，选取最小可读字节
        size_t available_to_read = ring_buffer_->AvailableToRead();
        size_t min_read_bytes = std::min(available_to_read, BATCH_SIZE);

        if (min_read_bytes > 0) {
          ring_buffer_->Read(read_buffer, min_read_bytes);
          RotateFileIfNeeded();
          file_manager_.file.write(
              reinterpret_cast<const char *>(read_buffer.data()),
              min_read_bytes * sizeof(uint8_t));

          CUR_WRITE_BYTES += min_read_bytes;
        }
        // 超过最大写入字节 flush
        if (min_read_bytes >= MAX_FLUSH_BYTES) {
          file_manager_.file.flush();
        }
      }
    }
    // 退出循环 flush
    file_manager_.file.flush();
  };

public:
  Logger()
      : ini_reader_(std::make_unique<IniReader>(CONFIG_PATH)),
        ring_buffer_(
            std::make_unique<containers::RingBuffer>(RING_BUFFER_SIZE)) {
    UpdateConfig();
    config_monitor_ =
        std::make_unique<std::thread>([this] { MonitorConfigChanges(); });
    cust_thread_ = std::make_unique<std::thread>([this] { CustThreadProc(); });
  }

  ~Logger() {
    monitor_config_thread_running_ = false;
    cust_thread_running_ = false;

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
  void LogCout(LogLevel level, const char *func, size_t line, Args &&...args) {
    if (!ShouldLog(level))
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << CurrentTime() << " " << LevelToString(level);
    if (config_.print_func)
      oss << "[" << func << " ";
    if (config_.print_line)
      oss << "L" << line << "] ";
    ((oss << std::forward<Args>(args)), ...) << "\n";

    std::cout << oss.str();

    if (level != MSG) {
      std::lock_guard<std::mutex> lock(ring_buffer_mutex_);
      ring_buffer_->Write(
          reinterpret_cast<const std::byte *>(oss.str().c_str()),
          oss.str().length());
      cv.notify_one();
    }
  }

  void LogPrint(LogLevel level, const char *func, size_t line,
                const char *format, ...) {
    if (!ShouldLog(level))
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::ostringstream oss;
    oss << CurrentTime() << " " << LevelToString(level);
    if (config_.print_func)
      oss << "[" << func << " ";
    if (config_.print_line)
      oss << "L" << line << "] ";
    oss << buffer << "\n";

    std::cout << oss.str();

    if (level != MSG) {
      std::lock_guard<std::mutex> lock(ring_buffer_mutex_);
      ring_buffer_->Write(
          reinterpret_cast<const std::byte *>(oss.str().c_str()),
          oss.str().length());
      cv.notify_one();
    }
  }
  template <typename T>
  void LogVector(LogLevel level, const char *func, size_t line,
                 const std::vector<T> &vector) {
    std::ostringstream oss;
    oss << CurrentTime() << " " << LevelToString(level);
    if (config_.print_func)
      oss << "[" << func << " ";
    if (config_.print_line)
      oss << "L" << line << "] ";

    for (size_t i = 0; i < vector.size(); ++i) {
      if (i != 0) // 首行不加
        oss << ",";
      if (sizeof(vector[i]) == 1)
        oss << (size_t)vector[i];
      else
        oss << vector[i];
    }
    oss << "\n";

    std::cout << oss.str();
  }

  static Logger &Instance() {
    static Logger instance;
    return instance;
  }
};

#define LOG_MSG(...)                                                           \
  Logger::Instance().LogCout(Logger::MSG, __func__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)                                                          \
  Logger::Instance().LogCout(Logger::INFO, __func__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)                                                          \
  Logger::Instance().LogCout(Logger::WARN, __func__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...)                                                         \
  Logger::Instance().LogCout(Logger::DEBUG, __func__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)                                                         \
  Logger::Instance().LogCout(Logger::ERROR, __func__, __LINE__, __VA_ARGS__)

#define LOGP_MSG(fmt, ...)                                                     \
  Logger::Instance().LogPrint(Logger::MSG, __func__, __LINE__, fmt,            \
                              ##__VA_ARGS__)
#define LOGP_INFO(fmt, ...)                                                    \
  Logger::Instance().LogPrint(Logger::INFO, __func__, __LINE__, fmt,           \
                              ##__VA_ARGS__)
#define LOGP_WARN(fmt, ...)                                                    \
  Logger::Instance().LogPrint(Logger::WARN, __func__, __LINE__, fmt,           \
                              ##__VA_ARGS__)
#define LOGP_DEBUG(fmt, ...)                                                   \
  Logger::Instance().LogPrint(Logger::DEBUG, __func__, __LINE__, fmt,          \
                              ##__VA_ARGS__)
#define LOGP_ERROR(fmt, ...)                                                   \
  Logger::Instance().LogPrint(Logger::ERROR, __func__, __LINE__, fmt,          \
                              ##__VA_ARGS__)

#define LOG_VECTOR(vector)                                                     \
  Logger::Instance().LogVector(Logger::MSG, __func__, __LINE__, vector)
