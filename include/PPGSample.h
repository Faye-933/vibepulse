/**
 * @file PPGSample.h
 * @brief 与硬件无关的 PPG 样本类型。
 *
 * 这个结构原来定义在 Sensor.h 里，而 Sensor.h 会 #include <gpiod.h>。
 * 结果是：任何想处理 PPG 数据的代码（DSP、回放源、单元测试）都被迫链接 libgpiod，
 * 即使它根本碰不到硬件。
 *
 * 把它单独提出来之后，采集无关的模块可以在没有 libgpiod 的机器上编译和测试。
 */

#ifndef VIBEPULSE_PPG_SAMPLE_H
#define VIBEPULSE_PPG_SAMPLE_H

/** @brief 单帧 PPG 样本：红光与红外通道的原始值（18 位，0..262143）。 */
struct Sample {
    float red = 0.0f;
    float ir = 0.0f;
};

#endif // VIBEPULSE_PPG_SAMPLE_H
