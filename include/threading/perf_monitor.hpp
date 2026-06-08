#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define DEFAULT_RECORD_SWITCH true

class PerfMonitor {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = std::chrono::nanoseconds;

private:
    std::string           name_;
    uint64_t              total_points_;
    Duration              total_proc_time_;
    uint64_t              total_full_cycles_;
    Duration              total_full_time_;
    bool                  record_individual_;
    std::vector<Duration> point_durations_;
    mutable std::mutex    mutex_;

    TimePoint start_proc_;
    TimePoint start_full_;

    explicit PerfMonitor(const std::string& name)
        : name_(name),
          total_points_(0),
          total_proc_time_(Duration::zero()),
          total_full_cycles_(0),
          total_full_time_(Duration::zero()),
          record_individual_(DEFAULT_RECORD_SWITCH),
          point_durations_(),
          start_proc_(),
          start_full_() {}

public:
    PerfMonitor(const PerfMonitor&) = delete;
    PerfMonitor& operator=(const PerfMonitor&) = delete;

    static PerfMonitor& GetInstance(const std::string& name = "default") {
        static std::unordered_map<std::string, std::unique_ptr<PerfMonitor>> instances;
        static std::mutex                                                    map_mutex;

        std::lock_guard<std::mutex> lock(map_mutex);
        auto                        it = instances.find(name);
        if (it == instances.end()) {
            std::unique_ptr<PerfMonitor> ptr(new PerfMonitor(name));
            it = instances.insert(std::make_pair(name, std::move(ptr))).first;
        }
        return *(it->second);
    }

    void EnableIndividualRecording(bool enable = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        record_individual_ = enable;
        if (!enable) {
            point_durations_.clear();
        }
    }

    void BeginFullCycle() {
        start_full_ = Clock::now();
    }

    void EndFullCycle() {
        TimePoint                   end = Clock::now();
        Duration                    dur = std::chrono::duration_cast<Duration>(end - start_full_);
        std::lock_guard<std::mutex> lock(mutex_);
        total_full_time_ += dur;
        ++total_full_cycles_;
        if (record_individual_) {
            point_durations_.push_back(dur);
        }
    }

    void BeginProcessing() {
        start_proc_ = Clock::now();
    }

    void EndProcessing() {
        TimePoint                   end = Clock::now();
        Duration                    dur = std::chrono::duration_cast<Duration>(end - start_proc_);
        std::lock_guard<std::mutex> lock(mutex_);
        total_proc_time_ += dur;
        ++total_points_;
        if (record_individual_) {
            point_durations_.push_back(dur);
        }
    }

    struct ProcessingStats {
        double   avg_ms;
        double   min_ms;
        double   max_ms;
        uint64_t count;
        double   total_ms;

        ProcessingStats() : avg_ms(0.0), min_ms(0.0), max_ms(0.0), count(0), total_ms(0.0) {}
    };

    struct FullCycleStats {
        double   avg_ms;
        double   min_ms;
        double   max_ms;
        uint64_t count;
        double   total_ms;

        FullCycleStats() : avg_ms(0.0), min_ms(0.0), max_ms(0.0), count(0), total_ms(0.0) {}
    };

    double GetLastProcessingTimeMs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record_individual_ && !point_durations_.empty()) {
            return static_cast<double>(point_durations_.back().count()) / 1000000.0;
        }
        return 0.0;
    }

    double GetLastFullCycleTimeMs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record_individual_ && !point_durations_.empty()) {
            return static_cast<double>(point_durations_.back().count()) / 1000000.0;
        }
        return 0.0;
    }

    ProcessingStats GetProcessingStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        ProcessingStats             s;
        s.count = total_points_;
        if (s.count == 0) {
            return s;
        }

        auto to_ms = [](const Duration& d) -> double { return static_cast<double>(d.count()) / 1000000.0; };

        s.total_ms = to_ms(total_proc_time_);
        s.avg_ms   = s.total_ms / static_cast<double>(s.count);

        if (record_individual_ && !point_durations_.empty()) {
            auto minmax = std::minmax_element(point_durations_.begin(), point_durations_.end());
            s.min_ms    = to_ms(*(minmax.first));
            s.max_ms    = to_ms(*(minmax.second));
        } else {
            s.min_ms = s.avg_ms;
            s.max_ms = s.avg_ms;
        }
        return s;
    }

    FullCycleStats GetFullCycleStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        FullCycleStats              s;
        s.count = total_full_cycles_;
        if (s.count == 0) {
            return s;
        }

        auto to_ms = [](const Duration& d) -> double { return static_cast<double>(d.count()) / 1000000.0; };

        s.total_ms = to_ms(total_full_time_);
        s.avg_ms   = s.total_ms / static_cast<double>(s.count);

        s.min_ms = s.avg_ms;
        s.max_ms = s.avg_ms;

        return s;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        total_points_      = 0;
        total_proc_time_   = Duration::zero();
        total_full_cycles_ = 0;
        total_full_time_   = Duration::zero();
        point_durations_.clear();
    }
};