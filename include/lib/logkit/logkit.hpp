#pragma once
#include "./ini_reader.hpp"

#include <atomic>
#include <chrono>
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

class LogKit {
public:
  enum LogLevel { MSG, INFO, WARN, DEBUG, ERROR };

private:
  static constexpr const char *CONFIG_PATH = "./configs/log_config.ini";
  static constexpr const char *GLOBAL_SECTION = "LOG_GLOBAL";
  static constexpr const char *LEVEL_SECTION = "LOG_LEVEL";

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
  std::atomic<bool> running_{true};
  std::unique_ptr<std::thread> config_monitor_;
  std::unique_ptr<IniReader> ini_reader_;

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
    while (running_) {
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

public:
  LogKit() : ini_reader_(std::make_unique<IniReader>(CONFIG_PATH)) {
    UpdateConfig();
    config_monitor_ =
        std::make_unique<std::thread>([this] { MonitorConfigChanges(); });
  }

  ~LogKit() {
    running_ = false;
    if (config_monitor_ && config_monitor_->joinable()) {
      config_monitor_->join();
    }
    if (file_manager_.file.is_open()) {
      file_manager_.file.close();
    }
  }

  template <typename... Args>
  void Log(LogLevel level, const char *func, size_t line, Args &&...args) {
    if (!ShouldLog(level))
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << CurrentTime() << " " << LevelToString(level);
    if (config_.print_func)
      oss << func << " ";
    if (config_.print_line)
      oss << "L" << line << " ";
    ((oss << std::forward<Args>(args)), ...) << "\n";

    std::cout << oss.str();

    if (level != MSG) {
      RotateFileIfNeeded();
      file_manager_.file << oss.str();
    }
  }

  void LogF(LogLevel level, const char *func, size_t line, const char *format,
            ...) {
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
      oss << func << " ";
    if (config_.print_line)
      oss << "L" << line << " ";
    oss << buffer << "\n";

    std::cout << oss.str();

    if (level != MSG) {
      RotateFileIfNeeded();
      file_manager_.file << oss.str();
    }
  }

  static LogKit &Instance() {
    static LogKit instance;
    return instance;
  }
};

#define LOG_MSG(...)                                                           \
  LogKit::Instance().Log(LogKit::MSG, __func__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)                                                          \
  LogKit::Instance().Log(LogKit::INFO, __func__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)                                                          \
  LogKit::Instance().Log(LogKit::WARN, __func__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...)                                                         \
  LogKit::Instance().Log(LogKit::DEBUG, __func__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)                                                         \
  LogKit::Instance().Log(LogKit::ERROR, __func__, __LINE__, __VA_ARGS__)

#define LOGF_MSG(fmt, ...)                                                     \
  LogKit::Instance().LogF(LogKit::MSG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGF_INFO(fmt, ...)                                                    \
  LogKit::Instance().LogF(LogKit::INFO, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGF_WARN(fmt, ...)                                                    \
  LogKit::Instance().LogF(LogKit::WARN, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGF_DEBUG(fmt, ...)                                                   \
  LogKit::Instance().LogF(LogKit::DEBUG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGF_ERROR(fmt, ...)                                                   \
  LogKit::Instance().LogF(LogKit::ERROR, __func__, __LINE__, fmt, ##__VA_ARGS__)
