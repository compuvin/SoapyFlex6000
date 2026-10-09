#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

// A 2.4 kHz complex baseband signal is placed into the positive 300-2700 Hz
// audio passband. DIGU then maps that passband above the slice dial frequency.
// The Soapy TX frequency is the RF frequency corresponding to complex DC;
// the radio slice must be tuned 1500 Hz below it.
class IqToDaxAudio {
public:
    static constexpr int sampleRate = 24000;
    static constexpr int audioOffsetHz = 1500;
    static constexpr int usableBandwidthHz = 2400;

    void setInputRate(int rate)
    {
        if (rate != 24000 && rate != 48000 && rate != 96000 && rate != 192000)
            throw std::runtime_error("Unsupported TX sample rate");
        decimation_ = rate / sampleRate;
        reset();
    }

    void reset()
    {
        oscillatorRe_ = 1.0; oscillatorIm_ = 0.0; samples_ = 0;
        inputCount_ = 0; sumI_ = sumQ_ = 0;
    }

    bool nextInputProducesOutput() const { return inputCount_ + 1 == decimation_; }

    std::optional<std::int16_t> feed(float i, float q)
    {
        if (!std::isfinite(i) || !std::isfinite(q)) { i = 0; q = 0; }
        sumI_ += i; sumQ_ += q;
        if (++inputCount_ != decimation_) return std::nullopt;
        const auto audio = convert(float(sumI_ / decimation_), float(sumQ_ / decimation_));
        inputCount_ = 0; sumI_ = sumQ_ = 0;
        return audio;
    }

    std::int16_t convert(float i, float q)
    {
        if (!std::isfinite(i) || !std::isfinite(q)) { i = 0; q = 0; }
        const double audio = double(i) * oscillatorRe_ - double(q) * oscillatorIm_;
        const double nextRe = oscillatorRe_ * rotationRe_ - oscillatorIm_ * rotationIm_;
        oscillatorIm_ = oscillatorRe_ * rotationIm_ + oscillatorIm_ * rotationRe_;
        oscillatorRe_ = nextRe;
        if ((++samples_ & 1023) == 0) {
            const double length = std::hypot(oscillatorRe_, oscillatorIm_);
            oscillatorRe_ /= length;
            oscillatorIm_ /= length;
        }
        return static_cast<std::int16_t>(std::lround(std::clamp(audio, -1.0, 1.0) * 32767.0));
    }

private:
    static constexpr double rotationRe_ = 0.9238795325112867; // cos(pi/8)
    static constexpr double rotationIm_ = 0.3826834323650898; // sin(pi/8)
    double oscillatorRe_ = 1.0, oscillatorIm_ = 0.0;
    unsigned samples_ = 0;
    int decimation_ = 1, inputCount_ = 0;
    double sumI_ = 0, sumQ_ = 0;
};
