/**
 * @file replay_demo.cpp
 * @brief Day 2 的验收程序：不接硬件，用回放源跑通「采样 → BPM」整条链路。
 *
 * 这是"最细的一条竖线"。它跑通之后，后面所有开发都不再被硬件和快递阻塞。
 *
 * 用法：
 *   ./vibepulse_replay                      # 合成 72 BPM / 30 秒
 *   ./vibepulse_replay --bpm 80 --seconds 60
 *   ./vibepulse_replay --csv data/ppg.csv   # 回放录制数据
 *   ./vibepulse_replay --fast               # 不按实时节拍（只验证功能）
 *   ./vibepulse_replay --normalize          # 先归一到 0..1 再送进 DSP
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#include "HeartRateCalculator.h"
#include "ReplaySource.h"

namespace {

void usage(const char* prog) {
    std::printf(
        "用法:\n"
        "  %s [选项]\n\n"
        "选项:\n"
        "  --bpm <数值>      合成信号的心率，默认 72\n"
        "  --seconds <数值>  合成信号时长，默认 30\n"
        "  --rate <数值>     采样率 Hz，默认 50（与 v1 一致）\n"
        "  --csv <路径>      改为回放 CSV（单列 ir，或 red,ir）\n"
        "  --fast            不按实时节拍投递，尽快跑完\n"
        "  --normalize       先把原始值除以 262143 归一到 0..1\n"
        "  -h, --help        显示本帮助\n",
        prog);
}

}  // namespace

int main(int argc, char** argv) {
    double bpm = 72.0;
    double seconds = 30.0;
    double rate = 50.0;
    std::string csv_path;
    bool realtime = true;
    bool normalize = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "参数 %s 缺少取值\n", name);
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--bpm") {
            bpm = std::atof(next_value("--bpm").c_str());
        } else if (arg == "--seconds") {
            seconds = std::atof(next_value("--seconds").c_str());
        } else if (arg == "--rate") {
            rate = std::atof(next_value("--rate").c_str());
        } else if (arg == "--csv") {
            csv_path = next_value("--csv");
        } else if (arg == "--fast") {
            realtime = false;
        } else if (arg == "--normalize") {
            normalize = true;
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    ReplaySource::Config cfg;
    cfg.sample_rate_hz = rate;
    cfg.batch_size = 1;
    cfg.realtime = realtime;
    cfg.loop = false;

    try {
        std::unique_ptr<ReplaySource> source =
            csv_path.empty() ? ReplaySource::synthetic(bpm, seconds, cfg)
                             : ReplaySource::fromCsv(csv_path, cfg);

        HeartRateCalculator calculator(rate);

        const auto t_start = std::chrono::steady_clock::now();
        unsigned bpm_updates = 0;

        calculator.setBpmCallback([&](double value) {
            ++bpm_updates;
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
            std::printf("[%7.2f s] BPM = %5.1f\n", elapsed, value);
            std::fflush(stdout);
        });

        source->setCallback([&](const std::vector<Sample>& batch) {
            std::vector<float> ir_values;
            ir_values.reserve(batch.size());
            for (const Sample& s : batch) {
                // MAX30102 是 18 位，满量程 2^18-1。v1 的检测阈值（如 peak_threshold=0.001）
                // 是按归一化量纲定的；如果直接喂原始值，阈值就形同虚设。
                ir_values.push_back(normalize ? s.ir / 262143.0f : s.ir);
            }
            calculator.processIrSamples(ir_values);
        });

        std::printf("回放开始：%.1f Hz，%s\n", rate,
                    csv_path.empty() ? "合成信号" : csv_path.c_str());
        std::fflush(stdout);

        source->start();
        while (!source->finished()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        source->stop();

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();

        std::printf("\n---- 汇总 ----\n");
        std::printf("投递帧数    : %zu\n", source->emitted());
        std::printf("耗时        : %.2f s （%.2f 帧/秒）\n", elapsed,
                    elapsed > 0 ? static_cast<double>(source->emitted()) / elapsed : 0.0);
        std::printf("BPM 更新次数 : %u\n", bpm_updates);
        if (auto latest = calculator.getLatestBpm()) {
            std::printf("最终 BPM    : %.1f\n", *latest);
        } else {
            std::printf("最终 BPM    : 未产生（信号未触发峰值检测）\n");
        }
        std::printf("手指检测    : %s\n", calculator.fingerDetected() ? "有" : "无");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
