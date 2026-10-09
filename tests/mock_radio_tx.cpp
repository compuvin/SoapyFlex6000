#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char *message)
{ if (!condition) throw std::runtime_error(message); }

uint32_t be32(const unsigned char *p)
{ return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

int listener(int type, int port)
{
    int fd = socket(AF_INET, type, 0);
    require(fd >= 0, "socket failed");
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(uint16_t(port));
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
        close(fd); throw std::runtime_error("mock radio port unavailable");
    }
    if (type == SOCK_STREAM && listen(fd, 1) != 0) {
        close(fd); throw std::runtime_error("mock radio listen failed");
    }
    return fd;
}

bool starts(const std::string &s, const std::string &prefix)
{ return s.compare(0, prefix.size(), prefix) == 0; }

size_t indexOf(const std::vector<std::string> &commands, const std::string &prefix)
{
    for (size_t i = 0; i < commands.size(); ++i)
        if (starts(commands[i], prefix)) return i;
    throw std::runtime_error("missing radio command: " + prefix);
}

void serve(int listening, std::vector<std::string> &commands, std::string &error)
{
    int client = accept(listening, nullptr, nullptr);
    if (client < 0) { error = "accept failed"; return; }
    timeval timeout{5, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const std::string banner = "V1.4.0.0\nH12345678\n";
    send(client, banner.data(), banner.size(), MSG_NOSIGNAL);
    std::string pending;
    int rfPower = 50;
    for (;;) {
        char buffer[2048];
        ssize_t n = recv(client, buffer, sizeof buffer, 0);
        if (n <= 0) break;
        pending.append(buffer, size_t(n));
        for (;;) {
            auto end = pending.find('\n');
            if (end == std::string::npos) break;
            const std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            const auto separator = line.find('|');
            if (line.empty() || line[0] != 'C' || separator == std::string::npos) {
                error = "malformed command"; break;
            }
            const std::string id = line.substr(1, separator - 1);
            const std::string cmd = line.substr(separator + 1);
            commands.push_back(cmd);
            if (starts(cmd, "transmit set rfpower=")) rfPower = std::stoi(cmd.substr(21));
            std::string result;
            if (cmd == "meter list") result = "meter 17.src=TX-#17.num=3#17.nam=SWR#17.low=1#17.hi=999#17.desc=RF SWR#17.unit=SWR#17.fps=20#18.src=TX-#18.num=4#18.nam=PATEMP#18.low=0#18.hi=100#18.desc=PA Temperature#18.unit=degC#18.fps=0#";
            else if (starts(cmd, "display pan c ")) result = "0x40000001,0x42000001";
            else if (starts(cmd, "display pan rfgain_info ")) result = "-10,40,10";
            else if (cmd == "stream create type=dax_iq daxiq_channel=1") result = "0x20000001";
            else if (cmd == "stream create type=dax_tx") result = "0x20000002";
            else if (starts(cmd, "slice create ")) result = "0";
            else if (cmd == "radio uptime") result = "123";
            else if (starts(cmd, "client ") || starts(cmd, "display pan s ") ||
                     starts(cmd, "display pan remove ") || starts(cmd, "dax iq set ") ||
                     starts(cmd, "stream set ") || starts(cmd, "stream remove ") ||
                     starts(cmd, "slice set ") || starts(cmd, "slice t ") ||
                     starts(cmd, "slice remove ") || starts(cmd, "transmit set dax=") ||
                     starts(cmd, "transmit set rfpower=") ||
                     cmd == "sub meter all" || cmd == "unsub meter all" ||
                     starts(cmd, "xmit ") || cmd == "sub tx all") {}
            else { error = "unexpected radio command: " + cmd; break; }
            const std::string reply = "R" + id + "|0|" + result + "\n";
            send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
            if (cmd == "sub tx all" || starts(cmd, "transmit set rfpower=")) {
                const std::string status = "S12345678|transmit lo=300 hi=3000 dax=0 rfpower=" + std::to_string(rfPower) + "\n";
                send(client, status.data(), status.size(), MSG_NOSIGNAL);
            }
        }
        if (!error.empty()) break;
    }
    close(client);
}
}

int main()
{
    try {
        int tcp = listener(SOCK_STREAM, 4992);
        int udp = listener(SOCK_DGRAM, 4991);
        std::vector<std::string> commands;
        std::string serverError;
        std::thread server(serve, tcp, std::ref(commands), std::ref(serverError));
        try {
            SoapySDR::Kwargs args{{"driver", "flex6000"}, {"addr", "127.0.0.1"}};
            auto *radio = SoapySDR::Device::make(args);
            require(radio != nullptr, "Soapy device missing");
            try {
                require(radio->listGains(SOAPY_SDR_TX, 0) == std::vector<std::string>{"RF"},
                        "TX RF gain not advertised");
                require(radio->getGainRange(SOAPY_SDR_TX, 0).minimum() == -20 &&
                        radio->getGainRange(SOAPY_SDR_TX, 0).maximum() == 0,
                        "TX gain range wrong");
                require(std::abs(radio->getGain(SOAPY_SDR_TX, 0) + 3.0102999566) < 0.001,
                        "initial TX gain readback wrong");
                radio->setGain(SOAPY_SDR_TX, 0, -10);
                require(std::abs(radio->getGain(SOAPY_SDR_TX, 0) + 10) < 0.001,
                        "TX gain round trip wrong");
                radio->setGain(SOAPY_SDR_TX, 0, -std::numeric_limits<double>::infinity());
                const double offGain = radio->getGain(SOAPY_SDR_TX, 0);
                require(std::isinf(offGain) && offGain < 0, "TX RF off readback wrong");
                radio->setGain(SOAPY_SDR_TX, 0, 0);
                require(radio->getGain(SOAPY_SDR_TX, 0) == 0,
                        "full TX RF gain readback wrong");
                const auto sensors = radio->listSensors();
                require(std::find(sensors.begin(), sensors.end(), "flex_meter_17_TX-_3") != sensors.end(),
                        "SWR meter missing");
                auto swrInfo = radio->getSensorInfo("flex_meter_17_TX-_3");
                require(swrInfo.name == "SWR" && swrInfo.units == "SWR" &&
                        swrInfo.range.minimum() == 1, "SWR metadata wrong");
                const auto portCommand = commands[indexOf(commands, "client udpport ")];
                const int meterPort = std::stoi(portCommand.substr(std::string("client udpport ").size()));
                sockaddr_in meterAddress{};
                meterAddress.sin_family = AF_INET;
                meterAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                meterAddress.sin_port = htons(uint16_t(meterPort));
                const std::array<unsigned char, 20> meterPacket{{
                    0x18,0x00,0x00,0x05, 0x80,0x00,0x00,0x00,
                    0x00,0x00,0x1c,0x2d, 0x53,0x4c,0x80,0x02,
                    0x00,0x11,0x01,0x00}};
                require(sendto(udp, meterPacket.data(), meterPacket.size(), 0,
                        reinterpret_cast<sockaddr *>(&meterAddress), sizeof meterAddress) ==
                        ssize_t(meterPacket.size()), "meter packet send failed");
                require(std::stod(radio->readSensor("flex_meter_17_TX-_3")) == 2.0,
                        "SWR meter value wrong");
                require(radio->getNumChannels(SOAPY_SDR_TX) == 1,
                        "DAX audio TX channel should be available by default");
                auto *rx = radio->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32);
                std::array<float, 1024> rxSamples{};
                void *rxBuffers[] = {rxSamples.data()};
                int rxFlags = 0;
                long long rxTime = 0;
                require(radio->readStream(rx, rxBuffers, 512, rxFlags, rxTime, 0) == SOAPY_SDR_TIMEOUT,
                        "RX read before activation must wait, not fail");
                require(radio->activateStream(rx) == 0, "RX activation failed");
                auto updatedMeter = meterPacket;
                updatedMeter[18] = 0x01; updatedMeter[19] = 0x80;
                require(sendto(udp, updatedMeter.data(), updatedMeter.size(), 0,
                        reinterpret_cast<sockaddr *>(&meterAddress), sizeof meterAddress) ==
                        ssize_t(updatedMeter.size()), "updated meter packet send failed");
                require(radio->readStream(rx, rxBuffers, 512, rxFlags, rxTime, 1000) == SOAPY_SDR_TIMEOUT,
                        "RX without packets should time out");
                require(std::stod(radio->readSensor("flex_meter_17_TX-_3")) == 3.0,
                        "RX read did not process meter packet");
                const std::array<unsigned char, 36> iqPacket{{
                    0x18,0xd0,0x00,0x09, 0x20,0x00,0x00,0x01,
                    0x00,0x00,0x1c,0x2d, 0x53,0x4c,0x00,0x00,
                    0,0,0,0, 0,0,0,0, 0,0,0,0,
                    0,0,0x80,0x3f, 0,0,0,0}};
                require(sendto(udp, iqPacket.data(), iqPacket.size(), 0,
                        reinterpret_cast<sockaddr *>(&meterAddress), sizeof meterAddress) ==
                        ssize_t(iqPacket.size()), "IQ packet send failed");
                require(radio->readStream(rx, rxBuffers, 1, rxFlags, rxTime, 100000) == 1 &&
                        rxSamples[0] == 1.0f && rxSamples[1] == 0.0f,
                        "RX IQ packet lost after meter packet");
                require(radio->deactivateStream(rx) == 0, "RX deactivation failed");
                require(radio->readStream(rx, rxBuffers, 512, rxFlags, rxTime, 0) == SOAPY_SDR_TIMEOUT,
                        "RX read after deactivation must wait, not fail");
                radio->closeStream(rx);
                radio->setSampleRate(SOAPY_SDR_TX, 0, 192000);
                radio->setBandwidth(SOAPY_SDR_TX, 0, 0);
                require(radio->getSampleRate(SOAPY_SDR_TX, 0) == 192000, "TX rate report wrong");
                require(radio->getBandwidth(SOAPY_SDR_TX, 0) == 2400, "TX bandwidth report wrong");
                auto *stream = radio->setupStream(SOAPY_SDR_TX, SOAPY_SDR_CF32);
                require(radio->activateStream(stream) == 0, "TX activation failed");
                std::array<float, 256> iq{};
                const void *buffers[] = {iq.data()};
                for (int packet = 0; packet < 375; ++packet) {
                    for (int n = 0; n < 128; ++n) {
                        double phase = 2 * 3.141592653589793 * 200 * (packet * 128 + n) / 192000;
                        iq[2*n] = float(0.05 * std::cos(phase));
                        iq[2*n+1] = float(0.05 * std::sin(phase));
                    }
                    int flags = 0;
                    require(radio->writeStream(stream, buffers, 128, flags, 0, 1000000) == 128,
                            "TX write failed");
                }
                require(radio->deactivateStream(stream) == 0, "TX unkey failed");
                radio->closeStream(stream);
            } catch (...) { SoapySDR::Device::unmake(radio); throw; }
            SoapySDR::Device::unmake(radio);
        } catch (...) {
            server.join(); close(tcp); close(udp); throw;
        }
        server.join();
        close(tcp);
        require(serverError.empty(), serverError.c_str());
        size_t created = indexOf(commands, "stream create type=dax_tx");
        size_t selected = indexOf(commands, "transmit set dax=1");
        size_t keyed = indexOf(commands, "xmit 1");
        size_t unkeyed = indexOf(commands, "xmit 0");
        size_t restored = indexOf(commands, "transmit set dax=0");
        require(created < selected && selected < keyed && keyed < unkeyed && unkeyed < restored,
                "TX control sequence wrong");
        std::array<unsigned char, 512> packet{};
        int count = 0;
        for (;;) {
            ssize_t n = recv(udp, packet.data(), packet.size(), MSG_DONTWAIT);
            if (n < 0) break;
            require(n == 284, "DAX packet size wrong");
            require((be32(packet.data()) & 0xffff) == 71, "VITA word count wrong");
            require(be32(packet.data() + 4) == 0x20000002, "DAX stream ID wrong");
            require(be32(packet.data() + 8) == 0x001c2d, "Flex OUI wrong");
            require(be32(packet.data() + 12) == 0x534c0123, "DAX audio class wrong");
            ++count;
        }
        close(udp);
        require(count == 46, "unexpected DAX packet count for 192 kS/s input");
        std::cout << "mock radio DAX TX sequence and packet format passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
