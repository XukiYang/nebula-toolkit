#pragma once
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

class IniReader {
public:
  explicit IniReader(const std::string &file_path) : file_path_(file_path) {}

  bool GetValue(const std::string &section, const std::string &key,
                bool &out_value) {
    std::string raw = GetProcessedValue(section, key);
    if (raw.empty())
      return false;

    raw = ToLower(raw);
    if (raw == "true" || raw == "1") {
      out_value = true;
      return true;
    }
    if (raw == "false" || raw == "0") {
      out_value = false;
      return true;
    }
    return false;
  }

  bool GetValue(const std::string &section, const std::string &key,
                size_t &out_value) {
    std::string raw = GetProcessedValue(section, key);
    if (raw.empty())
      return false;

    try {
      out_value = std::stoul(raw);
      return true;
    } catch (...) {
      return false;
    }
  }

  bool GetValue(const std::string &section, const std::string &key,
                std::string &out_value) {
    std::string raw = GetProcessedValue(section, key);
    if (raw.empty())
      return false;

    out_value = raw;
    return true;
  }

private:
  std::string file_path_;

  std::string GetProcessedValue(const std::string &section,
                                const std::string &key) {
    std::string raw = GetRawValue(section, key);
    return Trim(SplitComment(raw));
  }

  std::string GetRawValue(const std::string &section, const std::string &key) {
    std::ifstream file(file_path_);
    if (!file.is_open())
      return "";

    bool in_section = false;
    std::string line;

    while (std::getline(file, line)) {
      line = Trim(line);
      if (line.empty())
        continue;

      if (IsSectionHeader(line)) {
        in_section = (ExtractSectionName(line) == section);
        continue;
      }

      if (in_section) {
        auto [current_key, value] = SplitKeyValue(line);
        if (current_key == key) {
          return value;
        }
      }
    }
    return "";
  }

  static bool IsSectionHeader(const std::string &line) {
    return line.front() == '[' && line.back() == ']';
  }

  static std::string ExtractSectionName(const std::string &line) {
    return line.substr(1, line.size() - 2);
  }

  static std::pair<std::string, std::string>
  SplitKeyValue(const std::string &line) {
    size_t pos = line.find('=');
    if (pos == std::string::npos)
      return {"", ""};

    return {Trim(line.substr(0, pos)), Trim(line.substr(pos + 1))};
  }

  static std::string SplitComment(const std::string &s) {
    size_t pos = s.find(';');
    return (pos == std::string::npos) ? s : s.substr(0, pos);
  }

  static std::string Trim(const std::string &s) {
    auto start = s.find_first_not_of(" \t");
    if (start == std::string::npos)
      return "";

    auto end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
  }

  static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  }
};