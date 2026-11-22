# VibePulse

Real-time heart-rate-driven adaptive music system on Raspberry Pi / Linux, in C++17.

A PPG sensor (MAX30102) is sampled over I2C on the sensor's data-ready interrupt, the
heart rate is estimated on the fly, and music tempo is driven by a closed control loop
that always stays **at or below** the measured heart rate.

> **Status: v2 in progress.** The `main` branch currently holds the v1 coursework
> baseline plus the first batch of refactoring (sample-source abstraction, replay
> source, deterministic tests). Timing numbers below are placeholders until the
> benchmark harness lands.

---

## Origin

This project started as a five-person team coursework for **ENG5220 Real Time Embedded
Programming** at the University of Glasgow. The original team repository is
[Zonlywyh/ENG5220-VibePulse](https://github.com/Zonlywyh/ENG5220-VibePulse), published
under the MIT license.

I am continuing it as a personal project. The work in v2 is a rewrite of the real-time
core: replacing the polling fallback with pure event-driven acquisition, replacing the
SDL2 mixer with a direct ALSA engine, adding a closed-loop tempo controller, and adding
the quantitative latency measurement the original project never had.

Original code remains MIT licensed; see [LICENSE](LICENSE). Credit for the v1 baseline
belongs to the original team.

---

## What it does

1. **Acquire** — MAX30102 FIFO is read only when its DRDY line fires, via
   `libgpiod` edge events multiplexed in an `epoll` loop. No polling.
2. **Estimate** — first-order high-pass (DC removal) + low-pass smoothing, envelope-
   adaptive peak detection, sub-sample peak interpolation, physiological range gating.
3. **Control** — a rate-limited, dead-banded follower computes a target tempo that is
   always ≤ the current heart rate, with anti-windup and finger-loss handling.
4. **Play** — tempo is realised through a pre-rendered tempo ladder with beat-aligned
   crossfades, driven by a frame-counting beat clock (no drift).

The three real-time layers are deliberately separated:

| Layer | Timescale | Requirement |
|---|---|---|
| Hard real-time | 5 ms (audio period) | no allocation, no locks in the audio callback |
| Soft real-time | 1–50 ms (acquisition) | event-driven, bounded wake-up latency |
| Control | 10–30 s | matches the physiological response of heart rate |

---

## Repository layout

```
include/    public headers (interfaces first)
src/        implementations
tests/      Google Test suites (no hardware required)
docs/       architecture, product notes, benchmarks
```

Key interfaces:

- `ISampleSource` — anything that produces PPG samples (real sensor or replay)
- `ReplaySource` — deterministic playback of recorded/synthetic PPG, used for testing
- `HeartRateEstimator` — samples in, BPM and finger state out
- `TempoController` — heart rate in, tempo out (the control law)
- `IAudioSink` — audio output abstraction (ALSA on device, null in CI)

---

## Build

Dependencies (Debian / Raspberry Pi OS):

```bash
sudo apt install -y build-essential cmake git libgpiod-dev libgtest-dev
```

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Run the tests:

```bash
ctest --test-dir build --output-on-failure
```

> `libgpiod` **2.x** is required — the code uses the v2 API
> (`gpiod_chip_request_lines`, `gpiod_line_request_get_fd`).
> Debian 12 / Ubuntu 22.04 ship 1.6 and will not compile.

---

## Roadmap

- [x] Import v1 baseline
- [x] `ISampleSource` abstraction + replay source + deterministic tests
- [ ] Remove the 20 ms polling fallback — acquisition driven purely by DRDY
- [ ] ALSA engine replacing SDL2 (fixed period, xrun accounting)
- [ ] Closed-loop tempo control (`tempo <= heart rate` invariant)
- [ ] End-to-end latency measurement (GPIO marker + recorded audio alignment)
- [ ] Benchmark table in `docs/`

---

## Known issues (v1 baseline, being fixed in v2)

- Acquisition contains a 20 ms `epoll_wait` timeout that unconditionally reads the
  FIFO, which degrades the event-driven design into 50 Hz polling.
- I2C transfer return values are not checked; short reads silently produce zeroed
  samples.
- Audio goes through SDL2 mixer (and therefore the desktop audio stack on the Pi),
  so end-to-end latency is neither bounded nor measured.
- `MusicPlayer` / `ZoneMusicPlayer` are legacy and are being replaced by the ALSA engine.

---

## License

MIT — see [LICENSE](LICENSE).
