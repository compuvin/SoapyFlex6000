# FLEX-6000 ↔ SoapySDR capability checklist

This tracks Flex capabilities in two groups: controls with a direct, established SoapySDR operation, and values that a generic Soapy API could carry but whose meaning or key remains Flex-specific. A checked box means the current `main` driver implements the described behavior; it does not imply that every FLEX-6000 model and configuration has been verified.

## Established Soapy controls

These operations have the same basic meaning on both sides of the bridge. Stream activation is included because it is the standard Soapy operation that starts and stops transmission.

- [x] **RX center frequency** — `setFrequency(RX)` maps to `display pan set … center=`. Implemented for the DAX IQ panadapter.
- [x] **TX center frequency** — `setFrequency(TX)` maps to `slice t …`. The DIGU slice tunes 1,500 Hz below Soapy's requested RF center.
- [x] **RX sample rate** — `setSampleRate(RX)` maps to `dax iq set … rate=` and stream rate. Supports 24, 48, 96, and 192 kS/s.
- [x] **RX RF gain** — `setGain(RX, "RF", dB)` maps to panadapter `rfgain=`; the range comes from `rfgain_info`. The radio chooses the corresponding preamp/attenuator state.
- [ ] **Runtime RX antenna** — `setAntenna(RX)` would map to panadapter `ant=`. The FLEX-6700 rejected both `display pan set` and `display pan s` with `5000002D`; investigate a slice-based route before claiming this works.
- [x] **TX antenna** — `setAntenna(TX)` maps to `slice set … txant=`. ANT1/ANT2 are advertised on the TX channel.
- [x] **TX start/stop** — `activateStream(TX)` and `deactivateStream(TX)` use `xmit 1/0`, DAX source, and stream. The driver restores the prior DAX source on normal stop.

Sources: [Soapy device API](https://github.com/pothosware/SoapySDR/blob/master/include/SoapySDR/Device.hpp), [Flex panadapter](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-display-pan), [Flex DAX](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-dax), [Flex slice](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-slice).

## Soapy API could carry

### Standard calls with a driver-specific interpretation

These use established Soapy methods or device arguments, but are not a direct adjustment of the corresponding Flex hardware control.

- [x] **TX input sample rate** — `setSampleRate(TX)` accepts 24, 48, 96, and 192 kS/s. The driver reduces higher input rates to the fixed 24 kS/s DAX TX rate; the radio's TX audio rate does not change.
- [x] **Initial RX antenna** — Device `antenna=` argument maps to `display pan c … ant=`. This is a construction argument, rather than runtime Soapy `setAntenna(RX)`; ANT2 device creation succeeded on the FLEX-6700.
- [x] **TX usable bandwidth** — `getBandwidth(TX)` and `listBandwidths(TX)` report a fixed 2.4 kHz DAX audio limit. `setBandwidth(TX, 0 or 2400)` accepts the fixed choice; it does not alter the radio's TX filter width.
- [ ] **Radio-reported frequency limits** — `getFrequencyRange` is standard capability readback, not a control. The probe currently prints an empty range; discover a truthful model-specific range before advertising one.

### Flex-specific settings

These would be named entries in `getSettingInfo` and accessed through `writeSetting` / `readSetting`, rather than pretending that a radio percentage or filter edge is a Soapy gain in dB. None is exposed yet. Radio-wide settings should report their current value and should not be silently reset when the Soapy device closes.

- [ ] **`tx_rf_power`** — `transmit set rfpower=` controls radio power/drive from 0–100. This is not Soapy `setGain(TX)` in dB. Read back before changing.
- [ ] **`tx_filter_low`, `tx_filter_high`** — `transmit set filter_low=`, `filter_high=` control global TX audio filter edges. Read and preserve prior values. The fixed Soapy TX bandwidth is only a capability claim.
- [ ] **`tx_tune_power`, `tx_tune`** — `transmit set tunepower=` and `transmit tune on/off` control station tuning separately from normal sample transmission.
- [ ] **`atu_start`, `atu_bypass`** — `atu start` and `atu bypass` are available only when the radio has an ATU and is ready. Readback should be a sensor/setting if supported.
- [ ] **`interlock_timeout`** — `interlock timeout=` is a radio-wide safety setting; expose only with accurate readback and ownership semantics.
- [ ] **`tx_filter` / `tx_source` readback** — `sub tx all` status. The driver already reads DAX source internally for restoration; public readback needs clear scope.
- [ ] **`dax_channel` runtime change** — `dax iq set … pan= … rate=`. Currently a device argument. Runtime reassignment would require a stream transition.

Sources: [Flex transmit](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-transmit), [Flex ATU](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-atu), [Flex interlock](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-interlock), [Soapy settings API](https://github.com/pothosware/SoapySDR/blob/master/include/SoapySDR/Device.hpp).

### Flex meters through Soapy sensors

Soapy has a standard sensor API (`listSensors` / `getSensorInfo` / `readSensor`), but does not define universal names for these Flex meters. The driver currently exposes **no** Soapy sensors. Use Flex meter IDs plus source/ordinal in internal keys so meters with the same display name do not collide. Preserve the radio's units instead of relabeling a value without conversion. Do not report an old sample as current after its meter disappears.

The radio's meter catalog is **dynamic**. `meter list` supplies each meter's ID, source, name, units, range, and update rate; slice meters can appear or disappear when modes and slices change. The named meters below are those in FlexRadio's published example, not a fixed inventory for every radio. A complete FLEX-6700 inventory requires a live `meter list` capture. No IP address or firmware version should be required in this mapping.

- [ ] **RF output** — `FWDPWR`, `REFPWR`, `SWR`. TX channel sensors when TX exists; otherwise device sensors. Forward/reflected power are documented in dBm.
- [ ] **PA health** — `PATEMP`. Device sensor, °C.
- [ ] **Supply** — `+13.8A`, `+13.8B`. Device sensors, volts before/after the fuse.
- [ ] **Transmit audio and processing** — `MICPEAK`, `MIC`, `CODEC`, `SC_MIC`, `COMPPEAK`, `SC_FILT_1`, `ALC`, `SC_FILT_2`, `B4RAMP`, `AFRAMP`, `GAIN`, `HWALC`. TX channel or device sensors, usually dBFS; availability depends on mode and source.
- [ ] **Slice receive levels** — `24kHz`, `OSC`, `LEVEL`, `NR/ANF`, `AGC+`. RX channel sensors only if associated with this driver's slice; this DAX IQ RX path does not currently own an RX slice. Other slices may still appear as device sensors.
- [ ] **Other advertised Flex meters** — Every valid entry from `meter list`. Device sensors, discovered at runtime rather than hard-coded.
- [ ] **Transmit readiness** — Interlock state/reason from status. Device or TX channel string/boolean sensors; status readback, not a VITA meter.
- [ ] **Reference/clock lock** — `pll_done`, oscillator lock status when supplied. Device boolean sensors; availability varies by model and installed reference hardware.
- [ ] **Radio uptime** — `radio uptime`. Device sensor in seconds; diagnostic readback, not a VITA meter.

Meter implementation checklist:

- [ ] Read `meter list` metadata and handle catalog changes.
- [ ] Subscribe to meter updates; decode VITA meter packets by ID and declared units.
- [ ] Attribute radio, TX, panadapter, and slice meters to the correct Soapy scope.
- [ ] Expose units, display names, and ranges through `getSensorInfo`.
- [ ] Define freshness behavior for missing or stale updates.
- [ ] Validate the actual FLEX-6700 meter catalog and values on the LAN.

Sources: [Flex metering protocol](https://github.com/flexradio/smartsdr-api-docs/wiki/Metering-Protocol), [Flex radio status and uptime](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-radio), [Soapy sensor API](https://github.com/pothosware/SoapySDR/blob/master/include/SoapySDR/Device.hpp).

## Controls that should not be claimed for the current IQ bridge

- RX `setBandwidth`: the panadapter's display span is not an independent analog/DAX IQ filter. The DAX IQ sample rate defines the delivered sample span; a slice audio filter would not filter the raw IQ stream.
- RX `setGainMode` / slice AGC, squelch, NR, ANF, WNB, APF, and audio level: these operate on a demodulated slice audio chain, while this driver's RX samples come from DAX IQ. Exposing them as IQ controls would mislead applications.
- TX `setGain` in dB: `rfpower` is a radio power/drive percentage, not a calibrated gain stage in dB.
- Soapy hardware time and timed commands: the current DAX streams do not provide a verified shared hardware timestamp, so the driver reports no timestamps.
- Soapy DC offset, IQ balance, frequency correction, and clock-source setters: no verified radio command mapping for this bridge. A radio lock state may still be useful as a read-only sensor.
- Generic GPIO: Flex TX relay and interlock configuration is station-specific, not a demonstrated arbitrary GPIO bank.

The Flex API also contains radio administration, network, GUI, CW, equalizer, and accessory commands. Soapy's generic settings mechanism could carry them, but they are not part of this SDR stream bridge until a concrete application use and safe readback/ownership behavior are defined.
