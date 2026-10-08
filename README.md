# SoapyFlex6000

A small, receive-only SoapySDR module for FLEX-6000 radios. It creates a DAX IQ stream and passes complex float samples to the SoapySDR application. Signal processing stays in the application.

## Build

Requires a C++17 compiler, CMake, and SoapySDR development headers.

```sh
cmake -S . -B build
cmake --build build
sudo cmake --install build
```

For an uninstalled build, set `SOAPY_SDR_PLUGIN_PATH` to the build directory. Run this from the project directory, even if you normally work elsewhere:

```sh
export SOAPY_SDR_PLUGIN_PATH="$PWD/build"
SoapySDRUtil --info | grep flex6000
SoapySDRUtil --find="driver=flex6000,addr=RADIO_IP"
SoapySDRUtil --probe="driver=flex6000,addr=RADIO_IP"
```

Replace `RADIO_IP` with your radio's IPv4 address. If you used another build directory, put its absolute path in `SOAPY_SDR_PLUGIN_PATH`. `--find` with an explicit address only checks that SoapySDR loaded the module; `--probe` actually connects to the radio.

A desktop application does not inherit environment variables exported in a separate terminal. To use the module without setting `SOAPY_SDR_PLUGIN_PATH`, install it with `sudo cmake --install build`, then restart the application.

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

The module exposes one RX channel in `CF32` format. Frequency, sample rate, antenna, and the radio's panadapter RF gain can be changed through SoapySDR. The RF gain range is read from the connected radio. It creates its own GUI client and panadapter; the radio's available client and DAX IQ resources still apply. DAX IQ is a receive path, so the stable module does not expose TX.

This has been compiled against SoapySDR 0.8.1 and probed successfully with a FLEX-6700. Receive sample streaming remains unverified. This driver's transmit path is still experimental and is not exposed by the stable module.

## Protocol references

- [nDAX](https://github.com/kc2g-flex-tools/nDAX): Flex command and VITA UDP patterns.
- [nCAT](https://github.com/kc2g-flex-tools/nCAT): Flex tuning and client control patterns.
- [M0LTE.Flex DAX IQ](https://github.com/M0LTE/M0LTE.Flex): DAX IQ setup sequence and payload order.
- [AetherSDR](https://github.com/aethersdr/AetherSDR): additional FLEX-6000 control and streaming reference.
