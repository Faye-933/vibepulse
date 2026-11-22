/**
 * @file ISampleSource.h
 * @brief 采集源的抽象接口。
 *
 * 这是 VibePulse v2 的支点：上层（DSP / BPM / 音频）只依赖这个接口，
 * 不依赖 MAX30102、不依赖 I2C、不依赖 libgpiod。
 *
 * 由此带来两件事：
 *   1. 没有硬件也能跑完整链路（ReplaySource 喂数据）；
 *   2. 同一段数据可以跑两遍，结果必须完全一致（可回归测试）。
 *
 * 注意：接口刻意与现有 Max30102Sensor 的公开方法保持同签名，
 * 这样传感器类只需要加一个基类声明即可，不改任何调用点。
 */

#ifndef VIBEPULSE_I_SAMPLE_SOURCE_H
#define VIBEPULSE_I_SAMPLE_SOURCE_H

#include <functional>
#include <vector>

#include "PPGSample.h"

class ISampleSource {
public:
    /** @brief 一批新样本的回调类型。在采集线程中被调用。 */
    using Callback = std::function<void(const std::vector<Sample>&)>;

    virtual ~ISampleSource() = default;

    /** @brief 开始采集。非阻塞：内部起线程。 */
    virtual void start() = 0;

    /** @brief 停止采集并等待工作线程结束。 */
    virtual void stop() = 0;

    /** @brief 注册回调。须在 start() 之前调用。 */
    virtual void setCallback(Callback cb) = 0;
};

#endif // VIBEPULSE_I_SAMPLE_SOURCE_H
