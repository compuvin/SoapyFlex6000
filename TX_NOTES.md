# Transmit investigation

The stable `main` module exposes RX only. Do not use the experimental waveform branch to control a live radio yet.

## FLEX-6700 observations

The radio used for these checks reported SmartSDR 3.10.15.38923. This is diagnostic context, not a version gate for the driver.

- A first waveform prototype registered a new mode, created a DIGU slice, and then sent `slice set <index> mode=<waveform mode>`. The mode change returned `50002001`. The prototype never sent `xmit 1`.
- A later control-only check registered a new RAW waveform mode and created a slice directly with `slice create ... mode=<waveform mode>`. Every command returned success, including `slice remove` and `waveform remove`. The radio then rebooted. No PTT command or IQ packet was sent.

Successful command replies therefore do not establish that this waveform setup is safe on the tested radio. The check did not isolate whether registration, slice creation, slice removal, waveform removal, or their timing caused the reboot. Further live waveform tests are stopped pending a better explanation. A dummy load addresses RF output, but it does not protect against a radio control-plane crash.

## Separate transmit paths

The working transmit path in [AetherSDR](https://github.com/aethersdr/AetherSDR) uses a `dax_tx` audio stream and Flex PTT control. DAX TX carries transmit audio, so it cannot be presented as a generic SoapySDR complex IQ TX stream without additional modulation and bandwidth limits.

The [FlexRadio Waveform SDK](https://github.com/flexradio/waveform-sdk) describes complex sample transport through a waveform. The experimental branch follows that path but remains disabled. Any eventual Soapy TX channel must report its actual sample rate and usable RF bandwidth, manage PTT and stream lifetime, and pass a radio test without resets before it is enabled.
