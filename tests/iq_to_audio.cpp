#include "../src/IqToDaxAudio.hpp"
#include <array>
#include <cmath>
#include <iostream>

namespace {
constexpr double pi = 3.14159265358979323846;
double toneMagnitude(const std::array<std::int16_t, 24000> &audio, int hz)
{
    double re = 0, im = 0;
    for (size_t n = 0; n < audio.size(); ++n) {
        double phase = 2 * pi * hz * double(n) / IqToDaxAudio::sampleRate;
        re += double(audio[n]) * std::cos(phase);
        im -= double(audio[n]) * std::sin(phase);
    }
    return std::hypot(re, im) / audio.size();
}
bool checkTone(int inputRate, int basebandHz, int expectedAudioHz)
{
    IqToDaxAudio converter;
    converter.setInputRate(inputRate);
    std::array<std::int16_t, 24000> audio{};
    size_t output = 0;
    for (int n = 0; n < inputRate; ++n) {
        double phase = 2 * pi * basebandHz * double(n) / inputRate;
        auto sample = converter.feed(float(std::cos(phase) * 0.5), float(std::sin(phase) * 0.5));
        if (sample) audio[output++] = *sample;
    }
    if (output != audio.size()) return false;
    const double wanted = toneMagnitude(audio, expectedAudioHz);
    const double image = toneMagnitude(audio, 2 * IqToDaxAudio::audioOffsetHz - expectedAudioHz);
    return wanted > 7000 &&
        (expectedAudioHz == IqToDaxAudio::audioOffsetHz || image < wanted * 0.001);
}
}

int main()
{
    for (int rate : {24000, 48000, 96000, 192000}) {
        if (!checkTone(rate, -1200, 300) || !checkTone(rate, 0, 1500) || !checkTone(rate, 1200, 2700)) {
            std::cerr << "IQ to audio frequency placement failed at " << rate << " samples/s\n";
            return 1;
        }
    }
    std::cout << "IQ to audio frequency placement passed\n";
    return 0;
}
