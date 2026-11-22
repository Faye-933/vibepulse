/**
 * @file test_replay_source.cpp
 * @brief ReplaySource 的单元测试：确定性、批量投递、CSV 解析、节拍。
 *
 * 这些测试不需要硬件、不需要 libgpiod、不需要 root。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ReplaySource.h"

namespace {

std::vector<float> collectIr(const std::vector<Sample>& batch) {
    std::vector<float> out;
    out.reserve(batch.size());
    for (const Sample& s : batch) {
        out.push_back(s.ir);
    }
    return out;
}

}  // namespace

TEST(ReplaySourceTest, EmitsEverySampleExactlyOnce) {
    ReplaySource::Config cfg;
    cfg.sample_rate_hz = 100.0;
    cfg.batch_size = 1;
    cfg.realtime = false;

    auto source = ReplaySource::synthetic(72.0, 1.0, cfg);
    std::vector<float> received;
    source->setCallback([&](const std::vector<Sample>& batch) {
        const auto ir = collectIr(batch);
        received.insert(received.end(), ir.begin(), ir.end());
    });

    source->start();
    while (!source->finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    source->stop();

    EXPECT_EQ(received.size(), 100u);
    EXPECT_EQ(source->emitted(), 100u);
}

TEST(ReplaySourceTest, BatchesRespectConfiguredBatchSize) {
    ReplaySource::Config cfg;
    cfg.sample_rate_hz = 100.0;
    cfg.batch_size = 7;
    cfg.realtime = false;

    auto source = ReplaySource::synthetic(72.0, 1.0, cfg);
    std::size_t callbacks = 0;
    std::size_t total = 0;
    std::size_t max_batch = 0;

    source->setCallback([&](const std::vector<Sample>& batch) {
        ++callbacks;
        total += batch.size();
        max_batch = std::max(max_batch, batch.size());
    });

    source->start();
    while (!source->finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    source->stop();

    EXPECT_EQ(total, 100u);
    EXPECT_EQ(max_batch, 7u);
    EXPECT_EQ(callbacks, 15u);  // 14 * 7 + 2
}

TEST(ReplaySourceTest, SyntheticDataIsDeterministicForSameSeed) {
    ReplaySource::Config cfg;
    cfg.sample_rate_hz = 50.0;
    cfg.realtime = false;

    auto a = ReplaySource::synthetic(80.0, 5.0, cfg, /*seed=*/1234);
    auto b = ReplaySource::synthetic(80.0, 5.0, cfg, /*seed=*/1234);

    std::vector<float> ia;
    std::vector<float> ib;
    a->setCallback([&](const std::vector<Sample>& batch) {
        const auto v = collectIr(batch);
        ia.insert(ia.end(), v.begin(), v.end());
    });
    b->setCallback([&](const std::vector<Sample>& batch) {
        const auto v = collectIr(batch);
        ib.insert(ib.end(), v.begin(), v.end());
    });

    a->start();
    b->start();
    while (!a->finished() || !b->finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    a->stop();
    b->stop();

    ASSERT_EQ(ia.size(), ib.size());
    EXPECT_EQ(ia, ib) << "同一 seed 两次生成必须逐字节一致";
}

TEST(ReplaySourceTest, CsvSingleColumnIsParsedAsIr) {
    const std::string path = "test_ppg_single.csv";
    {
        std::ofstream out(path);
        out << "ir\n";          // 表头应被跳过
        out << "100000\n";
        out << "  # 注释行\n";
        out << "\n";
        out << "100500\n";
    }

    ReplaySource::Config cfg;
    cfg.realtime = false;
    auto source = ReplaySource::fromCsv(path, cfg);

    std::vector<float> ir;
    source->setCallback([&](const std::vector<Sample>& batch) {
        const auto v = collectIr(batch);
        ir.insert(ir.end(), v.begin(), v.end());
    });
    source->start();
    while (!source->finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    source->stop();

    std::remove(path.c_str());

    ASSERT_EQ(ir.size(), 2u);
    EXPECT_FLOAT_EQ(ir[0], 100000.0f);
    EXPECT_FLOAT_EQ(ir[1], 100500.0f);
}

TEST(ReplaySourceTest, CsvTwoColumnsAreRedAndIr) {
    const std::string path = "test_ppg_two.csv";
    {
        std::ofstream out(path);
        out << "90000,100000\n";
        out << "91000,101000\n";
    }

    ReplaySource::Config cfg;
    cfg.realtime = false;
    auto source = ReplaySource::fromCsv(path, cfg);

    std::vector<Sample> got;
    source->setCallback([&](const std::vector<Sample>& batch) {
        got.insert(got.end(), batch.begin(), batch.end());
    });
    source->start();
    while (!source->finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    source->stop();

    std::remove(path.c_str());

    ASSERT_EQ(got.size(), 2u);
    EXPECT_FLOAT_EQ(got[0].red, 90000.0f);
    EXPECT_FLOAT_EQ(got[0].ir, 100000.0f);
}

TEST(ReplaySourceTest, RealtimeModePacesAtSampleRate) {
    ReplaySource::Config cfg;
    cfg.sample_rate_hz = 100.0;
    cfg.batch_size = 1;
    cfg.realtime = true;

    auto source = ReplaySource::synthetic(72.0, 0.5, cfg);  // 50 帧 @100Hz ≈ 0.5 s
    const auto t0 = std::chrono::steady_clock::now();
    source->start();
    while (!source->finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    source->stop();
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    // 宽松区间：CI 机器慢也只是变长，不会变短
    EXPECT_GT(elapsed, 0.3);
    EXPECT_LT(elapsed, 2.0);
}
