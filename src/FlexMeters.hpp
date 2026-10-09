#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct FlexMeter {
    unsigned id = 0;
    std::string source, ordinal, name, description, units;
    double low = 0, high = 0, value = 0;
    int fps = 0;
    bool hasValue = false;
    std::chrono::steady_clock::time_point updated{};

    std::string key() const
    { return "flex_meter_" + std::to_string(id) + "_" + source + "_" + ordinal; }

    bool usable() const
    { return !source.empty() && !ordinal.empty() && !name.empty() && scale() > 0; }

    double scale() const
    {
        if (units == "dBm" || units == "dBFS" || units == "SWR") return 128;
        if (units == "Volts" || units == "amps") return 1024;
        if (units == "degC") return 64;
        return 0;
    }
};

class FlexMeters {
public:
    void catalog(const std::string &text)
    {
        std::map<unsigned, FlexMeter> next;
        size_t pos = text.find("meter ");
        if (pos == std::string::npos) {
            if (text.empty()) meters_.clear();
            return;
        }
        pos += 6;
        while (pos < text.size()) {
            const size_t end = text.find('#', pos);
            const std::string part = text.substr(pos, end == std::string::npos ? end : end - pos);
            const size_t dot = part.find('.'), equal = part.find('=');
            if (dot != std::string::npos && equal != std::string::npos && dot < equal) {
                try {
                    const std::string number = part.substr(0, dot);
                    if (!number.empty() && std::all_of(number.begin(), number.end(), [](unsigned char c){ return std::isdigit(c); })) {
                        unsigned id = std::stoul(number);
                        if (id <= 65535) {
                            auto &meter = next[id]; meter.id = id;
                            const std::string field = part.substr(dot + 1, equal - dot - 1);
                            std::string value = part.substr(equal + 1);
                            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                                value = value.substr(1, value.size() - 2);
                            if (field == "src") meter.source = value;
                            else if (field == "num") meter.ordinal = value;
                            else if (field == "nam") meter.name = value;
                            else if (field == "desc") meter.description = value;
                            else if (field == "unit") meter.units = value;
                            else if (field == "low") meter.low = std::stod(value);
                            else if (field == "hi") meter.high = std::stod(value);
                            else if (field == "fps") meter.fps = std::stoi(value);
                        }
                    }
                } catch (...) {} // Ignore malformed fields, not the rest of the catalog.
            }
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        for (auto &[id, meter] : next) {
            auto old = meters_.find(id);
            if (old != meters_.end() && old->second.source == meter.source &&
                old->second.ordinal == meter.ordinal && old->second.name == meter.name &&
                old->second.units == meter.units) {
                meter.value = old->second.value;
                meter.hasValue = old->second.hasValue;
                meter.updated = old->second.updated;
            }
        }
        meters_.swap(next);
    }

    void value(unsigned id, int16_t raw)
    {
        auto it = meters_.find(id);
        if (it == meters_.end() || !it->second.usable()) return;
        it->second.value = raw / it->second.scale();
        it->second.updated = std::chrono::steady_clock::now();
        it->second.hasValue = true;
    }

    const std::map<unsigned, FlexMeter> &all() const { return meters_; }

    const FlexMeter *find(const std::string &key) const
    {
        for (const auto &[id, meter] : meters_)
            if (meter.usable() && meter.key() == key) return &meter;
        return nullptr;
    }

private:
    std::map<unsigned, FlexMeter> meters_;
};
