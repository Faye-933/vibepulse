#include "ReplaySource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

ReplaySource::ReplaySource(std::vector<Sample> data, Config cfg)
    : data_(std::move(data)), cfg_(cfg) {
    if (data_.empty()) {
        throw std::invalid_argument("ReplaySource: sample set is empty");
    }
    if (!(cfg_.sample_rate_hz > 0.0)) {
        throw std::invalid_argument("ReplaySource: sample_rate_hz must be > 0");
    }
    if (cfg_.batch_size == 0) {
        cfg_.batch_size = 1;
    }
}

ReplaySource::~ReplaySource() {
    stop();
}

void ReplaySource::setCallback(Callback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    cb_ = std::move(cb);
}

void ReplaySource::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // 已在运行
    }
    finished_.store(false);
    worker_ = std::thread(&ReplaySource::run, this);
}

void ReplaySource::stop() {
    if (!running_.exchange(false)) {
        if (worker_.joinable()) {
            worker_.join();
        }
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ReplaySource::run() {
    using clock = std::chrono::steady_clock;
    using dsec = std::chrono::duration<double>;

    const dsec sample_period(1.0 / cfg_.sample_rate_hz);
    const auto t_start = clock::now();

    std::size_t idx = 0;
    while (running_.load()) {
        std::vector<Sample> batch;
        batch.reserve(cfg_.batch_size);

        for (std::size_t i = 0; i < cfg_.batch_size; ++i) {
            if (idx >= data_.size()) {
                if (!cfg_.loop) break;
                idx = 0;
            }
            batch.push_back(data_[idx]);
            ++idx;
        }

        if (batch.empty()) break;

        Callback cb;
        {
            std::lock_guard<std::mutex> lock(cb_mutex_);
            cb = cb_;
        }
        if (cb) {
            cb(batch);
        }
        emitted_.fetch_add(batch.size());

        if (!cfg_.loop && idx >= data_.size()) break;

        if (cfg_.realtime) {
            // 第 idx 个样本的理想时刻：t_start + idx * period
            const auto deadline =
                t_start + std::chrono::duration_cast<clock::duration>(sample_period * static_cast<double>(idx));
            const auto now = clock::now();
            if (deadline > now) {
                std::this_thread::sleep_until(deadline);
            }
        }
    }

    finished_.store(!cfg_.loop);
    running_.store(false);
}

std::unique_ptr<ReplaySource> ReplaySource::fromCsv(const std::string& path, Config cfg) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("ReplaySource: cannot open " + path);
    }

    std::vector<Sample> data;
    std::string line;
    std::size_t lineno = 0;

    while (std::getline(in, line)) {
        ++lineno;
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;

        std::stringstream ss(s);
        std::string a;
        std::string b;
        if (!std::getline(ss, a, ',')) continue;
        if (!std::getline(ss, b, ',')) b.clear();

        try {
            const double v1 = std::stod(trim(a));
            if (b.empty()) {
                Sample smp;
                smp.ir = static_cast<float>(v1);
                smp.red = 0.0f;
                data.push_back(smp);
            } else {
                const double v2 = std::stod(trim(b));
                Sample smp;
                smp.red = static_cast<float>(v1);
                smp.ir = static_cast<float>(v2);
                data.push_back(smp);
            }
        } catch (const std::exception&) {
            // 表头或脏行：跳过，不中断整个文件
            continue;
        }
    }

    if (data.empty()) {
        throw std::runtime_error("ReplaySource: no numeric samples parsed from " + path);
    }
    return std::unique_ptr<ReplaySource>(new ReplaySource(std::move(data), cfg));
}

std::unique_ptr<ReplaySource> ReplaySource::synthetic(double bpm,
                                                      double seconds,
                                                      Config cfg,
                                                      unsigned seed) {
    if (!(bpm > 0.0)) throw std::invalid_argument("ReplaySource: bpm must be > 0");
    if (!(seconds > 0.0)) throw std::invalid_argument("ReplaySource: seconds must be > 0");

    const double fs = cfg.sample_rate_hz;
    const auto n = static_cast<std::size_t>(std::llround(seconds * fs));
    if (n == 0) throw std::invalid_argument("ReplaySource: synthetic length rounds to 0");

    const double f0 = bpm / 60.0;
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 40.0);

    // 量纲对齐真实传感器：DC 反射光约 1e5，脉搏交流分量约 2e3（18 位量程）
    constexpr double kDc = 100000.0;
    constexpr double kAc = 2000.0;

    std::vector<Sample> data(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        // 基频 + 二次谐波，模拟收缩峰与舒张峰的双峰形态
        const double pulse = std::sin(2.0 * kPi * f0 * t) + 0.3 * std::sin(4.0 * kPi * f0 * t);
        const double v = kDc + kAc * pulse + noise(rng);
        data[i].ir = static_cast<float>(v);
        data[i].red = static_cast<float>(v * 0.9);  // 红光通道近似同比
    }

    return std::unique_ptr<ReplaySource>(new ReplaySource(std::move(data), cfg));
}
