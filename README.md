# SoapyFlex6000

A lightweight C++17 [SoapySDR](https://github.com/pothosware/SoapySDR) driver for FLEX-6000 series radios. It bridges the radio's DAX IQ receive stream into a SoapySDR application. Signal processing stays in the application.

The `main` branch provides RX and narrowband TX through DAX audio. Both channels are available by default; see [Transmit status](#transmit-status) for the TX limits.

The driver has been compiled against SoapySDR 0.8.1 and tested with a FLEX-6700. RX streaming through SoapyRemote worked in ChrissySDR, and changing RX RF gain produced a visible difference. The driver has also been compiled and run on a Raspberry Pi 3 with an ARM operating system.

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
| `tx_path` | `dax_audio` | TX path; set to `none` for an RX-only device |

The module exposes one RX channel in `CF32` format. Frequency, sample rate, and the radio's panadapter RF gain can be changed through SoapySDR. Select the initial RX antenna with the `antenna=` device argument; runtime antenna switching was rejected by the tested FLEX-6700. The RF gain range is read from the connected radio. It creates its own GUI client and panadapter; the radio's available client and DAX IQ resources still apply.

## Transmit status

The driver exposes one `CF32` TX channel by default. It accepts 24,000, 48,000, 96,000, or 192,000 samples/s and has 2,400 Hz of usable RF bandwidth. The driver reduces higher input rates to the radio's fixed 24,000 samples/s DAX audio rate, shifts complex baseband to a 1,500 Hz audio center, selects a DIGU slice 1,500 Hz below the Soapy TX center frequency, and sends 16-bit mono DAX audio. The Soapy application must keep its transmitted signal within ±1,200 Hz of its center frequency; the simple rate reduction is not a substitute for application bandlimiting. The radio's transmit filter must pass roughly 300–2,700 Hz for the full advertised width.

Setting up a TX stream creates and selects this client's TX slice and DAX audio source without keying the radio. TX starts when the application activates its TX stream and stops when it deactivates or closes that stream. The driver keeps the selected path between activations and attempts to restore the prior DAX audio source when the TX stream closes. Use `ANT1` or `ANT2` as the TX antenna. This path supports narrowband signals, not wideband IQ transmission.

Soapy TX `RF` gain now controls the radio's `rfpower` setting. The driver uses 0 dB for full relative drive and converts -20 to 0 dB to the radio's 1–100 scale using power ratios; `-inf` requests RF power off (0). `getGain(TX)` uses cached transmit status without making a synchronous radio request and rounds readback to 0.1 dB. A gain write sends one `transmit set` command; repeated requests for the same radio power are suppressed even if a later status disagrees. The radio-reported value remains visible through `getGain(TX)`. The slider only approximates output watts and the radio's Max Power limit may cap it, so the reported gain describes the radio-reported relative drive rather than calibrated RF output. This control passed an offline mock-radio test. On a FLEX-6700, read-only radio checks matched SDR Oxide's -17.0 dB, -3.0 dB, and 0.0 dB TX gain settings.

Direct SoapySDR tests transmitted successfully twice with a FLEX-6700 connected to a dummy load. RF output was not independently measured by the driver. Narrowband DAX audio TX is available on `main`, while broader model and application testing remains to be done.

On the tested FLEX-6700, changing the RX antenna after the panadapter is created currently returns radio error `5000002D`, although `ant list` includes ANT2. Set the initial receive antenna with the `antenna=` device argument until runtime RX antenna switching is resolved. This does not affect the TX antenna setting.

See the [Flex-to-Soapy capability checklist](SOAPY_CAPABILITY_CHECKLIST.md) for controls, meters, current support, and proposed mappings. Checklist entries are planned work unless checked.

## Metering preview

The driver now requests `meter list`, subscribes to meter updates, and exposes meters with recognized units as device-level Soapy sensors. Keys include the Flex meter ID, source, and source ordinal (for example, `flex_meter_17_TX-_3`); names, descriptions, units, and ranges come from the radio's catalog. `listSensors()` refreshes that catalog. `readSensor()` returns `nan` for a meter without a recent value rather than returning an old reading or failing the entire device probe. Meter and DAX IQ packets share the existing UDP connection.

If the radio reports an internal ATU, the driver advertises one device setting, `atu`, through SoapySDR's settings API. Writing `true` sends `atu start` and begins a tune cycle; writing `false` sends `atu bypass`. Reading the setting returns the radio's `atu_enabled` status. Starting a tune creates and selects a TX slice if this client does not already have one, so the radio must allow transmission at the selected frequency. This mapping passed an offline mock-radio test but has not yet been tried on the FLEX-6700.

This path passes an offline mock-radio test but has not yet been checked against the FLEX-6700's live meter catalog or values. All meters currently use device-level sensor scope; channel attribution and automatic catalog change notifications remain to be verified.

The experimental IQ waveform setup disrupted the radio's control service without PTT. See [transmit investigation notes](TX_NOTES.md) before using the separate `experimental/waveform-tx` branch.

## Protocol references

- [nDAX](https://github.com/kc2g-flex-tools/nDAX): Flex command and VITA UDP patterns.
- [nCAT](https://github.com/kc2g-flex-tools/nCAT): Flex tuning and client control patterns.
- [M0LTE.Flex DAX IQ](https://github.com/M0LTE/M0LTE.Flex): DAX IQ setup sequence and payload order.
- [AetherSDR](https://github.com/aethersdr/AetherSDR): additional FLEX-6000 control and streaming reference.
