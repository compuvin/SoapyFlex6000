# SoapyFlex6000

A small SoapySDR module for FLEX-6000 radios. It passes DAX IQ receive samples to the application. This experimental branch also offers opt-in, narrowband transmit through DAX audio.

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
| `tx_path` | unset | Set to `dax_audio` to expose the experimental TX channel |

The module exposes one RX channel in `CF32` format. Frequency, sample rate, and the radio's panadapter RF gain can be changed through SoapySDR. The initial RX antenna is selected with a device argument; runtime antenna switching has the limitation described below. The RF gain range is read from the connected radio. It creates its own GUI client and panadapter; the radio's available client and DAX IQ resources still apply.

This has been compiled against SoapySDR 0.8.1 and probed successfully with a FLEX-6700. Receive sample streaming remains unverified. The stable `main` branch remains RX only.

## Experimental DAX audio transmit

Set `tx_path=dax_audio` in the Soapy device arguments to expose one `CF32` TX channel. It accepts 24,000, 48,000, 96,000, or 192,000 samples/s and has 2,400 Hz of usable RF bandwidth. The driver reduces higher input rates to the radio's fixed 24,000 samples/s DAX audio rate, shifts complex baseband to a 1,500 Hz audio center, selects a DIGU slice 1,500 Hz below the Soapy TX center frequency, and sends 16-bit mono DAX audio. The Soapy application must keep its transmitted signal within ±1,200 Hz of its center frequency; the simple rate reduction is not a substitute for application bandlimiting. The radio's transmit filter must pass roughly 300–2,700 Hz for the full advertised width.

TX starts when the application activates its TX stream and stops when it deactivates or closes that stream. The driver attempts to restore the radio's prior DAX audio source afterward. Use `ANT1` or `ANT2` as the TX antenna. This path supports narrowband signals, not wideband IQ transmission.

A three-second 1,700 Hz audio test using a FLEX-6700 completed with a TX indication and no apparent radio restart. RF output was not independently measured. This feature remains experimental; test into a suitable load before using an antenna.

On the tested FLEX-6700, changing the RX antenna after the panadapter is created currently returns radio error `5000002D`, although `ant list` includes ANT2. Set the initial receive antenna with the `antenna=` device argument until runtime RX antenna switching is resolved. This does not affect the TX antenna setting.

See the [Flex-to-Soapy capability checklist](SOAPY_CAPABILITY_CHECKLIST.md) for controls, meters, current support, and proposed mappings.

The experimental IQ waveform setup disrupted the radio's control service without PTT. See [transmit investigation notes](TX_NOTES.md) before using that branch.

## Protocol references

- [nDAX](https://github.com/kc2g-flex-tools/nDAX): Flex command and VITA UDP patterns.
- [nCAT](https://github.com/kc2g-flex-tools/nCAT): Flex tuning and client control patterns.
- [M0LTE.Flex DAX IQ](https://github.com/M0LTE/M0LTE.Flex): DAX IQ setup sequence and payload order.
- [AetherSDR](https://github.com/aethersdr/AetherSDR): additional FLEX-6000 control and streaming reference.
