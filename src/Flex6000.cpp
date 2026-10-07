#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
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
        ip_ = arg(args, "addr", ""); if (ip_.empty()) ip_ = discover();
        if (ip_.empty()) throw std::runtime_error("No Flex radio discovered; specify addr=IP");
        channel_ = std::stoi(arg(args, "dax_channel", "1"));
        if (channel_ < 1 || channel_ > 4) throw std::runtime_error("dax_channel must be 1..4");
        antenna_ = arg(args, "antenna", "ANT1");
        frequency_ = std::stod(arg(args, "frequency", "14074000"));
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
        command("client gui"); command("client program SoapyFlex6000");
        std::ostringstream mhz; mhz << std::fixed << std::setprecision(6) << frequency_ / 1e6;
        pan_ = parseId(firstField(command("display pan c freq=" + mhz.str() + " ant=" + antenna_)));
        command("display pan s " + hexId(pan_) + " daxiq_channel=" + std::to_string(channel_));
        configureRate();
        stream_ = parseId(firstField(command("stream create type=dax_iq daxiq_channel=" + std::to_string(channel_))));
        command("stream set " + hexId(stream_) + " daxiq_rate=" + std::to_string(rate_ / 1000));
    }
    ~Flex() override
    {
        if (tcp_ >= 0) {
            try { if (stream_) command("stream remove " + hexId(stream_)); } catch (...) {}
            try { if (pan_) command("display pan remove " + hexId(pan_)); } catch (...) {}
            close(tcp_);
        }
        if (udp_ >= 0) close(udp_);
    }
    std::string getDriverKey() const override { return "flex6000"; }
    std::string getHardwareKey() const override { return "FlexRadio FLEX-6000"; }
    SoapySDR::Kwargs getHardwareInfo() const override { return {{"addr", ip_}, {"dax_channel", std::to_string(channel_)}}; }
    size_t getNumChannels(const int direction) const override { return direction == SOAPY_SDR_RX ? 1 : 0; }
    std::vector<std::string> getStreamFormats(const int direction, const size_t) const override
    { return direction == SOAPY_SDR_RX ? std::vector<std::string>{SOAPY_SDR_CF32} : std::vector<std::string>{}; }
    std::string getNativeStreamFormat(const int, const size_t, double &fullScale) const override
    { fullScale = 1.0; return SOAPY_SDR_CF32; }
    std::vector<double> listSampleRates(const int, const size_t) const override
    { return {24000, 48000, 96000, 192000}; }
    void setSampleRate(const int direction, const size_t, const double rate) override
    { checkRx(direction); int r = int(std::lround(rate)); if (std::find(rates_.begin(), rates_.end(), r) == rates_.end()) throw std::runtime_error("Unsupported DAX IQ rate"); rate_ = r; configureRate(); }
    double getSampleRate(const int, const size_t) const override { return rate_; }
    void setFrequency(const int direction, const size_t, const double frequency, const SoapySDR::Kwargs &) override
    { checkRx(direction); if (frequency <= 0) throw std::runtime_error("Invalid frequency"); std::ostringstream s; s << std::fixed << std::setprecision(6) << frequency / 1e6; command("display pan set " + hexId(pan_) + " center=" + s.str()); frequency_ = frequency; }
    double getFrequency(const int, const size_t) const override { return frequency_; }
    std::vector<std::string> listAntennas(const int, const size_t) const override { return {"ANT1", "ANT2", "RX_A", "RX_B"}; }
    void setAntenna(const int direction, const size_t, const std::string &antenna) override
    { checkRx(direction); command("display pan set " + hexId(pan_) + " ant=" + antenna); antenna_ = antenna; }
    std::string getAntenna(const int, const size_t) const override { return antenna_; }
    SoapySDR::Stream *setupStream(const int direction, const std::string &format, const std::vector<size_t> &channels, const SoapySDR::Kwargs &) override
    { checkRx(direction); if (format != SOAPY_SDR_CF32 || (!channels.empty() && channels != std::vector<size_t>{0})) throw std::runtime_error("Only RX channel 0 CF32 is supported"); return reinterpret_cast<SoapySDR::Stream *>(this); }
    void closeStream(SoapySDR::Stream *) override { active_ = false; }
    size_t getStreamMTU(SoapySDR::Stream *) const override { return 512; }
    int activateStream(SoapySDR::Stream *, const int flags, const long long, const size_t) override
    { if (flags) return SOAPY_SDR_NOT_SUPPORTED; active_ = true; pending_.clear(); offset_ = 0; return 0; }
    int deactivateStream(SoapySDR::Stream *, const int, const long long) override { active_ = false; return 0; }
    int readStream(SoapySDR::Stream *, void * const *buffs, const size_t numElems, int &flags, long long &timeNs, const long timeoutUs) override
    {
        flags = 0; timeNs = 0;
        if (!active_ || !buffs || !buffs[0]) return SOAPY_SDR_STREAM_ERROR;
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
    int tcp_ = -1, udp_ = -1, channel_ = 1, rate_ = 48000;
    uint32_t serial_ = 0, pan_ = 0, stream_ = 0;
    double frequency_ = 14074000;
    bool active_ = false;
    std::string ip_, antenna_, received_;
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
