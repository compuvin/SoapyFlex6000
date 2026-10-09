#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include "IqToDaxAudio.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
uint16_t be16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t be32(const uint8_t *p) { return (uint32_t(be16(p)) << 16) | be16(p + 2); }
std::string arg(const SoapySDR::Kwargs &a, const std::string &k, const std::string &fallback)
{ auto it = a.find(k); return it == a.end() ? fallback : it->second; }
int udpSocket(unsigned port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) throw std::runtime_error("UDP socket failed");
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0)
    { close(fd); throw std::runtime_error("UDP bind failed"); }
    return fd;
}
std::string hexId(uint32_t id)
{ std::ostringstream s; s << "0x" << std::hex << id; return s.str(); }
uint32_t parseId(const std::string &s)
{ return static_cast<uint32_t>(std::stoul(s, nullptr, 16)); }
std::string firstField(const std::string &s)
{ return s.substr(0, s.find_first_of(",| ")); }
std::string discover()
{
    int fd = udpSocket(4992);
    pollfd p{fd, POLLIN, 0}; std::string ip;
    if (poll(&p, 1, 1200) > 0) {
        std::array<uint8_t, 4096> buf{};
        ssize_t n = recv(fd, buf.data(), buf.size(), 0);
        if (n > 28 && be16(buf.data() + 14) == 0xffff) {
            const std::string body(reinterpret_cast<char *>(buf.data() + 28), size_t(n) - 28);
            auto pos = body.find("ip=");
            if (pos != std::string::npos) ip = body.substr(pos + 3, body.find_first_of(" \0", pos + 3) - pos - 3);
        }
    }
    close(fd); return ip;
}
class Flex final : public SoapySDR::Device {
public:
    explicit Flex(const SoapySDR::Kwargs &args)
    {
        const std::string txPath = arg(args, "tx_path", "dax_audio");
        txEnabled_ = txPath == "dax_audio";
        if (!txEnabled_ && txPath != "none")
            throw std::runtime_error("tx_path must be dax_audio or none");
        ip_ = arg(args, "addr", ""); if (ip_.empty()) ip_ = discover();
        if (ip_.empty()) throw std::runtime_error("No Flex radio discovered; specify addr=IP");
        channel_ = std::stoi(arg(args, "dax_channel", "1"));
        if (channel_ < 1 || channel_ > 4) throw std::runtime_error("dax_channel must be 1..4");
        antenna_ = arg(args, "antenna", "ANT1");
        frequency_ = std::stod(arg(args, "frequency", "14074000"));
        txFrequency_ = frequency_;
        rate_ = std::stoi(arg(args, "rate", "48000"));
        if (std::find(rates_.begin(), rates_.end(), rate_) == rates_.end()) throw std::runtime_error("rate must be 24000, 48000, 96000 or 192000");
        udp_ = udpSocket(0);
        sockaddr_in local{}; socklen_t len = sizeof local;
        getsockname(udp_, reinterpret_cast<sockaddr *>(&local), &len);
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        addrinfo *result = nullptr;
        if (getaddrinfo(ip_.c_str(), "4992", &hints, &result) != 0) throw std::runtime_error("Cannot resolve radio address");
        tcp_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        bool connected = tcp_ >= 0 && connect(tcp_, result->ai_addr, result->ai_addrlen) == 0;
        freeaddrinfo(result);
        if (!connected) throw std::runtime_error("Cannot connect to Flex TCP port 4992");
        command("client udpport " + std::to_string(ntohs(local.sin_port)));
        command("client gui");
        try {
            const std::string info = command("info");
            const auto key = info.find("model=\"");
            if (key != std::string::npos) {
                const auto begin = key + 7;
                const auto end = info.find('"', begin);
                if (end != std::string::npos) model_ = info.substr(begin, end - begin);
            }
        } catch (...) {} // Older radios may not provide model information.
        std::ostringstream mhz; mhz << std::fixed << std::setprecision(6) << frequency_ / 1e6;
        pan_ = parseId(firstField(command("display pan c freq=" + mhz.str() + " ant=" + antenna_)));
        try {
            const std::string info = command("display pan rfgain_info " + hexId(pan_));
            std::stringstream fields(info); std::string value;
            if (std::getline(fields, value, ',')) rfGainMin_ = std::stod(value);
            if (std::getline(fields, value, ',')) rfGainMax_ = std::stod(value);
            if (std::getline(fields, value, ',')) rfGainStep_ = std::stod(value);
        } catch (...) { rfGainMin_ = rfGainMax_ = rfGainStep_ = 0; }
        command("display pan s " + hexId(pan_) + " daxiq_channel=" + std::to_string(channel_));
        configureRate();
        stream_ = parseId(firstField(command("stream create type=dax_iq daxiq_channel=" + std::to_string(channel_))));
        command("stream set " + hexId(stream_) + " daxiq_rate=" + std::to_string(rate_ / 1000));
        if (txEnabled_) {
            txAddress_.sin_family = AF_INET;
            txAddress_.sin_port = htons(4991);
            if (inet_pton(AF_INET, ip_.c_str(), &txAddress_.sin_addr) != 1)
                throw std::runtime_error("TX requires an IPv4 radio address");
        }
    }
    ~Flex() override
    {
        if (tcp_ >= 0) {
            try { stopTx(); } catch (...) {}
            try { if (txStream_) command("stream remove " + hexId(txStream_)); } catch (...) {}
            try { if (txSlice_ >= 0) command("slice remove " + std::to_string(txSlice_)); } catch (...) {}
            try { if (stream_) command("stream remove " + hexId(stream_)); } catch (...) {}
            try { if (pan_) command("display pan remove " + hexId(pan_)); } catch (...) {}
            close(tcp_);
        }
        if (udp_ >= 0) close(udp_);
    }
    std::string getDriverKey() const override { return "flex6000"; }
    std::string getHardwareKey() const override { return "FlexRadio FLEX-6000"; }
    bool getFullDuplex(const int, const size_t) const override { return false; }
    SoapySDR::Kwargs getHardwareInfo() const override { return {{"addr", ip_}, {"dax_channel", std::to_string(channel_)}}; }
    size_t getNumChannels(const int direction) const override { return direction == SOAPY_SDR_RX ? 1 : size_t(txEnabled_); }
    std::vector<std::string> getStreamFormats(const int direction, const size_t) const override
    { return direction == SOAPY_SDR_RX || txEnabled_ ? std::vector<std::string>{SOAPY_SDR_CF32} : std::vector<std::string>{}; }
    std::string getNativeStreamFormat(const int, const size_t, double &fullScale) const override
    { fullScale = 1.0; return SOAPY_SDR_CF32; }
    std::vector<double> listSampleRates(const int direction, const size_t) const override
    { (void)direction; return {24000, 48000, 96000, 192000}; }
    void setSampleRate(const int direction, const size_t, const double rate) override
    { if (direction == SOAPY_SDR_TX) { if (!txEnabled_ || txActive_ || !std::isfinite(rate) || std::find(rates_.begin(), rates_.end(), rate) == rates_.end()) throw std::runtime_error("Unsupported TX sample rate"); txRate_ = int(rate); return; } checkRx(direction); int r = int(std::lround(rate)); if (std::find(rates_.begin(), rates_.end(), r) == rates_.end()) throw std::runtime_error("Unsupported DAX IQ rate"); rate_ = r; configureRate(); }
    double getSampleRate(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? txRate_ : rate_; }
    void setFrequency(const int direction, const size_t, const double frequency, const SoapySDR::Kwargs &) override
    { if (frequency <= 0) throw std::runtime_error("Invalid frequency"); if (direction == SOAPY_SDR_TX) { if (!txEnabled_ || txActive_) throw std::runtime_error("Cannot retune active TX"); txFrequency_ = frequency; if (txSlice_ >= 0) tuneTx(); return; } checkRx(direction); std::ostringstream s; s << std::fixed << std::setprecision(6) << frequency / 1e6; command("display pan set " + hexId(pan_) + " center=" + s.str()); frequency_ = frequency; }
    double getFrequency(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? txFrequency_ : frequency_; }
    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t) const override
    {
        if (direction != SOAPY_SDR_RX) return {};
        if (model_ == "FLEX-6300" || model_ == "FLEX-6400" || model_ == "FLEX-6400M" ||
            model_ == "FLEX-6600" || model_ == "FLEX-6600M") return {{30000, 54000000}};
        if (model_ == "FLEX-6500") return {{30000, 77000000}};
        if (model_ == "FLEX-6700" || model_ == "FLEX-6700R")
            return {{30000, 77000000}, {135000000, 175000000}};
        return {};
    }
    std::vector<std::string> listAntennas(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? std::vector<std::string>{"ANT1", "ANT2"} : std::vector<std::string>{"ANT1", "ANT2", "RX_A", "RX_B"}; }
    void setAntenna(const int direction, const size_t, const std::string &antenna) override
    { if (direction == SOAPY_SDR_TX) { if (!txEnabled_ || (antenna != "ANT1" && antenna != "ANT2") || txActive_) throw std::runtime_error("Invalid TX antenna or active TX"); txAntenna_ = antenna; if (txSlice_ >= 0) command("slice set " + std::to_string(txSlice_) + " txant=" + txAntenna_); return; } checkRx(direction); command("display pan s " + hexId(pan_) + " ant=" + antenna); antenna_ = antenna; }
    std::string getAntenna(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? txAntenna_ : antenna_; }
    std::vector<double> listBandwidths(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? std::vector<double>{2400} : std::vector<double>{}; }
    double getBandwidth(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? 2400 : 0; }
    void setBandwidth(const int direction, const size_t, const double bandwidth) override { if (direction != SOAPY_SDR_TX || !txEnabled_ || (bandwidth != 0 && bandwidth != 2400)) throw std::runtime_error("TX bandwidth is fixed at 2400 Hz"); }
    std::vector<std::string> listGains(const int direction, const size_t) const override
    { return direction == SOAPY_SDR_RX && rfGainMax_ > rfGainMin_ ? std::vector<std::string>{"RF"} : std::vector<std::string>{}; }
    void setGain(const int direction, const size_t, const double value) override
    { setGain(direction, 0, "RF", value); }
    void setGain(const int direction, const size_t, const std::string &name, const double value) override
    {
        checkRx(direction);
        if (name != "RF" || rfGainMax_ <= rfGainMin_) throw std::runtime_error("Unsupported gain control");
        if (!std::isfinite(value) || value < rfGainMin_ || value > rfGainMax_) throw std::runtime_error("RF gain out of range");
        double snapped = rfGainStep_ > 0 ? rfGainMin_ + std::round((value - rfGainMin_) / rfGainStep_) * rfGainStep_ : value;
        std::ostringstream gain; gain << snapped;
        command("display pan set " + hexId(pan_) + " rfgain=" + gain.str());
        rfGain_ = snapped;
    }
    double getGain(const int direction, const size_t) const override { return direction == SOAPY_SDR_TX ? 0 : rfGain_; }
    double getGain(const int direction, const size_t, const std::string &) const override { return getGain(direction, 0); }
    SoapySDR::Range getGainRange(const int direction, const size_t) const override
    { return direction == SOAPY_SDR_TX ? SoapySDR::Range(0, 0) : SoapySDR::Range(rfGainMin_, rfGainMax_, rfGainStep_); }
    SoapySDR::Range getGainRange(const int direction, const size_t, const std::string &) const override
    { return getGainRange(direction, 0); }
    SoapySDR::Stream *setupStream(const int direction, const std::string &format, const std::vector<size_t> &channels, const SoapySDR::Kwargs &) override
    { if (direction == SOAPY_SDR_TX && !txEnabled_) throw std::runtime_error("TX disabled by tx_path=none"); if (format != SOAPY_SDR_CF32 || (!channels.empty() && channels != std::vector<size_t>{0})) throw std::runtime_error("Only channel 0 CF32 is supported"); return direction == SOAPY_SDR_TX ? txHandle() : rxHandle(); }
    void closeStream(SoapySDR::Stream *stream) override { if (stream == txHandle()) stopTx(); else active_.store(false); }
    size_t getStreamMTU(SoapySDR::Stream *) const override { return 512; }
    int activateStream(SoapySDR::Stream *stream, const int flags, const long long, const size_t) override
    { if (flags) return SOAPY_SDR_NOT_SUPPORTED; if (stream == txHandle()) { startTx(); return 0; } ++rxGeneration_; active_.store(true); return 0; }
    int deactivateStream(SoapySDR::Stream *stream, const int, const long long) override { if (stream == txHandle()) return stopTx() ? 0 : SOAPY_SDR_STREAM_ERROR; active_.store(false); return 0; }
    int writeStream(SoapySDR::Stream *stream, const void * const *buffs, const size_t numElems, int &flags, const long long, const long timeoutUs) override
    {
        if (stream != txHandle() || !txActive_ || !buffs || !buffs[0] || flags) return SOAPY_SDR_STREAM_ERROR;
        const auto *iq = static_cast<const float *>(buffs[0]);
        size_t consumed = 0;
        const auto deadline = timeoutUs < 0 ? std::chrono::steady_clock::time_point::max() :
            std::chrono::steady_clock::now() + std::chrono::microseconds(timeoutUs);
        try {
            while (consumed < numElems) {
                if (converter_.nextInputProducesOutput() && txFill_ == txSamples_.size()-1 && nextPacket_ > deadline)
                    return consumed ? int(consumed) : SOAPY_SDR_TIMEOUT;
                auto audio = converter_.feed(iq[2*consumed], iq[2*consumed+1]);
                ++consumed;
                if (!audio) continue;
                txSamples_[txFill_++] = *audio;
                if (txFill_ != txSamples_.size()) continue;
                auto now = std::chrono::steady_clock::now();
                if (now < nextPacket_) std::this_thread::sleep_until(nextPacket_);
                sendTxPacket(); txFill_ = 0;
                nextPacket_ += std::chrono::microseconds(5333);
                if (nextPacket_ < std::chrono::steady_clock::now()) nextPacket_ = std::chrono::steady_clock::now();
            }
        } catch (...) { stopTx(); return SOAPY_SDR_STREAM_ERROR; }
        flags = 0;
        return int(consumed);
    }
    int readStream(SoapySDR::Stream *stream, void * const *buffs, const size_t numElems, int &flags, long long &timeNs, const long timeoutUs) override
    {
        if (stream != rxHandle()) return SOAPY_SDR_STREAM_ERROR;
        flags = 0; timeNs = 0;
        if (!buffs || !buffs[0]) return SOAPY_SDR_STREAM_ERROR;
        // SoapyRemote starts reading after setupStream, before activateStream.
        if (!active_.load()) return SOAPY_SDR_TIMEOUT;
        const unsigned generation = rxGeneration_.load();
        if (generation != observedRxGeneration_) { pending_.clear(); offset_ = 0; observedRxGeneration_ = generation; }
        auto *dst = static_cast<float *>(buffs[0]); size_t copied = 0;
        while (copied < numElems) {
            if (offset_ < pending_.size() / 2) {
                size_t count = std::min(numElems - copied, pending_.size() / 2 - offset_);
                std::memcpy(dst + copied * 2, pending_.data() + offset_ * 2, count * 2 * sizeof(float));
                copied += count; offset_ += count; continue;
            }
            pollfd p{udp_, POLLIN, 0};
            int waitMs = copied ? 0 : (timeoutUs < 0 ? -1 : int((timeoutUs + 999) / 1000));
            int ready = poll(&p, 1, waitMs);
            if (ready <= 0) return copied ? int(copied) : (ready == 0 ? SOAPY_SDR_TIMEOUT : SOAPY_SDR_STREAM_ERROR);
            std::array<uint8_t, 8192> packet{};
            ssize_t n = recv(udp_, packet.data(), packet.size(), 0);
            if (n < 32) continue;
            const auto *b = packet.data();
            uint32_t header = be32(b); uint8_t type = uint8_t(header >> 28);
            if (type != 1 && type != 3) continue;
            size_t bytes = std::min(size_t(n), size_t(header & 0xffff) * 4);
            size_t pos = 4;
            if (be32(b + pos) != stream_) continue;
            pos += 4;
            if (header & 0x08000000) pos += 8;
            if ((header >> 22 & 3) != 0) pos += 4;
            if ((header >> 20 & 3) != 0) pos += 8;
            if (header & 0x04000000) bytes -= 4;
            if (bytes <= pos || (bytes - pos) % 8) continue;
            pending_.resize((bytes - pos) / 4);
            for (size_t i = 0; i < pending_.size(); ++i) {
                uint32_t bits = uint32_t(b[pos + i*4]) | (uint32_t(b[pos + i*4 + 1]) << 8) |
                                (uint32_t(b[pos + i*4 + 2]) << 16) | (uint32_t(b[pos + i*4 + 3]) << 24);
                std::memcpy(&pending_[i], &bits, 4);
            }
            offset_ = 0;
        }
        return int(copied);
    }
private:
    SoapySDR::Stream *rxHandle() { return reinterpret_cast<SoapySDR::Stream *>(&rxTag_); }
    SoapySDR::Stream *txHandle() { return reinterpret_cast<SoapySDR::Stream *>(&txTag_); }
    void tuneTx() { std::ostringstream s; s << std::fixed << std::setprecision(6) << (txFrequency_ - IqToDaxAudio::audioOffsetHz) / 1e6; command("slice t " + std::to_string(txSlice_) + " " + s.str()); }
    void startTx()
    {
        if (!txEnabled_ || txActive_) return;
        try {
            txDaxStatus_.reset();
            previousDax_.reset();
            command("sub tx all");
            command("radio uptime");
            if (!txDaxStatus_) throw std::runtime_error("Cannot determine current transmit DAX source");
            previousDax_ = *txDaxStatus_;
            if (txSlice_ < 0) {
                std::ostringstream s; s << std::fixed << std::setprecision(6) << (txFrequency_ - IqToDaxAudio::audioOffsetHz) / 1e6;
                txSlice_ = std::stoi(firstField(command("slice create pan=" + hexId(pan_) + " freq=" + s.str() + " mode=DIGU")));
                command("slice set " + std::to_string(txSlice_) + " txant=" + txAntenna_);
                tuneTx();
            }
            command("client set send_reduced_bw_dax=true");
            if (!txStream_) txStream_ = parseId(firstField(command("stream create type=dax_tx")));
            command("stream set " + hexId(txStream_) + " tx=1");
            command("slice set " + std::to_string(txSlice_) + " tx=1");
            command("transmit set dax=1");
            command("xmit 1"); txActive_ = true;
            converter_.setInputRate(txRate_); txFill_ = 0; txCount_ = 0;
            nextPacket_ = std::chrono::steady_clock::now();
        } catch (...) { try { command("xmit 0"); } catch (...) {} try { if (previousDax_) command("transmit set dax=" + std::to_string(*previousDax_)); } catch (...) {} throw; }
    }
    bool stopTx()
    {
        if (!txActive_) return true;
        try { command("xmit 0"); } catch (...) { return false; }
        txActive_ = false;
        try { if (previousDax_) command("transmit set dax=" + std::to_string(*previousDax_)); } catch (...) {}
        txFill_ = 0;
        return true;
    }
    static void put32(uint8_t *p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }
    void sendTxPacket()
    {
        std::array<uint8_t, 284> packet{};
        put32(packet.data(), 0x18d00047u | (uint32_t(txCount_++ & 15) << 16));
        put32(packet.data()+4, txStream_);
        put32(packet.data()+8, 0x001c2d);
        put32(packet.data()+12, 0x534c0123);
        for (size_t i=0; i<txSamples_.size(); ++i) { auto v = uint16_t(txSamples_[i]); packet[28+2*i] = uint8_t(v >> 8); packet[29+2*i] = uint8_t(v); }
        if (sendto(udp_, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr *>(&txAddress_), sizeof txAddress_) != ssize_t(packet.size())) throw std::runtime_error("DAX TX UDP send failed");
    }
    void checkRx(int direction) const { if (direction != SOAPY_SDR_RX) throw std::runtime_error("Flex DAX IQ is RX only"); }
    void configureRate() { command("dax iq set " + std::to_string(channel_) + " pan=" + hexId(pan_) + " rate=" + std::to_string(rate_ / 1000)); if (stream_) command("stream set " + hexId(stream_) + " daxiq_rate=" + std::to_string(rate_ / 1000)); }
    std::string command(const std::string &cmd)
    {
        uint32_t id = ++serial_;
        std::string line = "C" + std::to_string(id) + "|" + cmd + "\n";
        size_t sent = 0;
        while (sent < line.size()) {
            ssize_t n = send(tcp_, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) throw std::runtime_error("Flex command send failed");
            sent += size_t(n);
        }
        for (;;) {
            auto end = received_.find('\n');
            if (end != std::string::npos) {
                std::string reply = received_.substr(0, end); received_.erase(0, end + 1);
                if (!reply.empty() && reply[0] == 'S') {
                    auto marker = reply.find("|transmit ");
                    if (marker != std::string::npos) {
                        auto dax = reply.find(" dax=", marker);
                        if (dax != std::string::npos && dax + 5 < reply.size() && (reply[dax+5] == '0' || reply[dax+5] == '1'))
                            txDaxStatus_ = reply[dax+5] - '0';
                    }
                }
                if (reply.empty() || reply[0] != 'R') continue;
                auto separator = reply.find('|');
                if (separator == std::string::npos || reply.substr(1, separator-1) != std::to_string(id)) continue;
                auto second = reply.find('|', separator + 1);
                std::string code = reply.substr(separator + 1, second - separator - 1);
                if (std::stoul(code, nullptr, 16) != 0) throw std::runtime_error("Flex command failed: " + cmd + " (" + code + ")");
                return second == std::string::npos ? "" : reply.substr(second + 1);
            }
            pollfd p{tcp_, POLLIN, 0};
            if (poll(&p, 1, 3000) <= 0) throw std::runtime_error("Flex command timeout: " + cmd);
            char buffer[4096]; ssize_t n = recv(tcp_, buffer, sizeof buffer, 0);
            if (n <= 0) throw std::runtime_error("Flex TCP connection closed");
            received_.append(buffer, size_t(n));
        }
    }
    int tcp_ = -1, udp_ = -1, channel_ = 1, rate_ = 48000, txRate_ = 24000;
    int rxTag_ = 0, txTag_ = 0, txSlice_ = -1;
    bool txEnabled_ = false, txActive_ = false;
    std::optional<int> txDaxStatus_, previousDax_;
    uint32_t txStream_ = 0, txCount_ = 0;
    double txFrequency_ = 14074000;
    std::string txAntenna_ = "ANT1";
    sockaddr_in txAddress_{};
    IqToDaxAudio converter_;
    std::array<int16_t, 128> txSamples_{};
    size_t txFill_ = 0;
    std::chrono::steady_clock::time_point nextPacket_{};
    uint32_t serial_ = 0, pan_ = 0, stream_ = 0;
    double frequency_ = 14074000;
    double rfGain_ = 0, rfGainMin_ = 0, rfGainMax_ = 0, rfGainStep_ = 0;
    std::atomic<bool> active_{false};
    std::atomic<unsigned> rxGeneration_{0};
    unsigned observedRxGeneration_ = 0;
    std::string ip_, antenna_, model_, received_;
    std::vector<float> pending_;
    size_t offset_ = 0;
    const std::array<int, 4> rates_{{24000, 48000, 96000, 192000}};
};
SoapySDR::KwargsList findFlex(const SoapySDR::Kwargs &args)
{
    std::string ip = arg(args, "addr", ""); if (ip.empty()) ip = discover();
    if (ip.empty()) return {};
    SoapySDR::Kwargs found = args; found["driver"] = "flex6000"; found["addr"] = ip;
    return {found};
}
SoapySDR::Device *makeFlex(const SoapySDR::Kwargs &args) { return new Flex(args); }
static SoapySDR::Registry registration("flex6000", &findFlex, &makeFlex, SOAPY_SDR_ABI_VERSION);
}
