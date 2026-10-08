# SoapyFlex6000

A lightweight C++17 [SoapySDR](https://github.com/pothosware/SoapySDR) driver for FLEX-6000 series radios. It bridges the radio's DAX IQ receive stream into a SoapySDR application. Signal processing stays in the application.

This `main` branch provides RX only. The `experimental/dax-audio-tx` branch adds opt-in narrowband TX through DAX audio; that feature remains experimental.

The driver has been compiled against SoapySDR 0.8.1 and probed with a FLEX-6700. Continuous receive streaming has not yet been verified on the radio.

## Build

Requires a C++17 compiler, CMake 3.16 or newer, and SoapySDR development headers and library. Install those through your Linux distribution before building.

```sh
cmake -S . -B build
cmake --build build
sudo cmake --install build
```

To use an uninstalled build, run these commands from the project directory. `SOAPY_SDR_PLUGIN_PATH` applies only to the current shell and processes started from it:

```sh
export SOAPY_SDR_PLUGIN_PATH="$PWD/build"
SoapySDRUtil --info
SoapySDRUtil --find="driver=flex6000"
SoapySDRUtil --probe="driver=flex6000,addr=RADIO_IP"
```

Replace `RADIO_IP` with your radio's IPv4 address. If you used another build directory, put its absolute path in `SOAPY_SDR_PLUGIN_PATH`. `--find` checks discovery and module loading; `--probe` connects to the radio and reports its capabilities. If UDP discovery does not work, include `addr=RADIO_IP` in `--find` as well.

A desktop application does not inherit environment variables exported in a separate terminal. Install the module with `sudo cmake --install build` and restart the application if you want it available through the system SoapySDR plugin path.

## Use

Connect the radio and computer to the same local network. Discovery listens for Flex UDP announcements on port 4992. If discovery is unavailable, set the address explicitly.

```sh
SoapySDRUtil --find="driver=flex6000"
SoapySDRUtil --probe="driver=flex6000,addr=RADIO_IP"
```

Device arguments:

| Argument | Default | Meaning |
| --- | --- | --- |
| `addr` | auto discovery | Radio IPv4 address |
| `frequency` | `14074000` | Initial panadapter center frequency, Hz |
| `rate` | `48000` | DAX IQ sample rate: 24000, 48000, 96000, or 192000 samples/s |
| `dax_channel` | `1` | DAX IQ channel, 1 through 4 |
| `antenna` | `ANT1` | Initial receive antenna |

The module exposes one RX channel in `CF32` format. Frequency, sample rate, and the radio's panadapter RF gain can be changed through SoapySDR. Select the initial RX antenna with the `antenna=` device argument; runtime antenna switching was rejected by the tested FLEX-6700. The RF gain range is read from the connected radio. It creates its own GUI client and panadapter; the radio's available client and DAX IQ resources still apply.

The experimental IQ waveform setup disrupted the radio's control service without PTT. See [transmit investigation notes](TX_NOTES.md) before using the `experimental/waveform-tx` branch.

## Protocol references

- [nDAX](https://github.com/kc2g-flex-tools/nDAX): Flex command and VITA UDP patterns.
- [nCAT](https://github.com/kc2g-flex-tools/nCAT): Flex tuning and client control patterns.
- [M0LTE.Flex DAX IQ](https://github.com/M0LTE/M0LTE.Flex): DAX IQ setup sequence and payload order.
- [AetherSDR](https://github.com/aethersdr/AetherSDR): additional FLEX-6000 control and streaming reference.
