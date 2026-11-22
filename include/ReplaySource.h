/**
 * @file ReplaySource.h
 * @brief 回放型采集源：用录制数据或合成信号替代真实传感器。
 *
 * 用途：
 *   - 没有硬件时开发与调试（Day 2 的交付物）；
 *   - 同一个 CSV 跑两遍，验证整条链路是确定性的；
 *   - 后续测「处理链路」的延迟时，作为可控的激励源。
 *
 * 它不是"假数据塞进去"那么简单：real-time 模式会按 sample_rate 的节拍
 * 逐批投递，行为和真实传感器的 50 Hz 回调一致，这样上层代码不需要区分两者。
 */

#ifndef VIBEPULSE_REPLAY_SOURCE_H
#define VIBEPULSE_REPLAY_SOURCE_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ISampleSource.h"
#include "PPGSample.h"

class ReplaySource final : public ISampleSource {
public:
    struct Config {
        /** 投递节拍，须与数据本身的采样率一致（v1 默认 50 Hz）。 */
        double sample_rate_hz = 50.0;
        /** 每次回调携带的样本数，模拟 FIFO 批量读。 */
        std::size_t batch_size = 1;
        /** true = 按真实节拍投递；false = 尽快吐完（仅用于功能验证）。 */
        bool realtime = true;
        /** true = 数据播完后从头循环，用于长时间稳定性测试。 */
        bool loop = false;
    };

    explicit ReplaySource(std::vector<Sample> data, Config cfg = {});
    ~ReplaySource() override;

    ReplaySource(const ReplaySource&) = delete;
    ReplaySource& operator=(const ReplaySource&) = delete;
    ReplaySource(ReplaySource&&) = delete;
    ReplaySource& operator=(ReplaySource&&) = delete;

    void start() override;
    void stop() override;
    void setCallback(Callback cb) override;

    /**
     * @brief 从 CSV 载入。每行一帧：
     *        单列 = ir；两列 = red,ir。
     *        空行与 # 开头的行忽略；首行表头（非数字）自动跳过。
     */
    static std::unique_ptr<ReplaySource> fromCsv(const std::string& path, Config cfg = {});

    /**
     * @brief 生成合成 PPG：基频 bpm/60，含二次谐波与高斯噪声，
     *        幅值按 MAX30102 的 18 位原始量程（DC≈1e5，AC≈2e3）。
     */
    static std::unique_ptr<ReplaySource> synthetic(double bpm,
                                                   double seconds,
                                                   Config cfg = {},
                                                   unsigned seed = 42);

    /** @brief 已投递的样本总数。 */
    std::size_t emitted() const { return emitted_.load(); }

    /** @brief 数据是否已播完（loop 模式下恒为 false）。 */
    bool finished() const { return finished_.load(); }

    const Config& config() const { return cfg_; }

private:
    void run();

    std::vector<Sample> data_;
    Config cfg_;
    Callback cb_;
    std::mutex cb_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    std::atomic<std::size_t> emitted_{0};
    std::thread worker_;
};

#endif // VIBEPULSE_REPLAY_SOURCE_H
