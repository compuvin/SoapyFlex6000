# Transmit investigation

The stable `main` module exposes RX only. Do not use the experimental waveform branch to control a live radio yet.

## FLEX-6700 observations

The radio used for these checks reported SmartSDR 3.10.15.38923. This is diagnostic context, not a version gate for the driver.

- A first waveform prototype registered a new mode, created a DIGU slice, and then sent `slice set <index> mode=<waveform mode>`. The mode change returned `50002001`. The prototype never sent `xmit 1`.
- A later control-only check registered a new RAW waveform mode and created a slice directly with `slice create ... mode=<waveform mode>`. Every command returned success, including `slice remove` and `waveform remove`. The radio appeared to reboot afterward. No PTT command or IQ packet was sent. A later uptime check showed that at least the subsequent monitored event was not a full radio reboot.

Successful command replies therefore do not establish that this waveform setup is safe on the tested radio. The check did not isolate whether registration, slice creation, slice removal, waveform removal, or their timing caused the control interruption. Further live waveform tests are stopped pending a better explanation. A dummy load addresses RF output, but it does not protect against a radio control-plane failure.

The one-off check did not run a waveform event loop or answer waveform commands and VITA packets. AetherSDR's waveform documentation describes a receive loop running before registration and responses to radio-originated commands. Those missing pieces and the rapid teardown are possible contributors, not confirmed causes. The experimental prototype must handle that lifecycle before another live check.

The experimental branch now starts TCP and UDP workers before waveform registration, answers radio-originated waveform commands, uses the named stream IDs returned by registration, returns silence for RX buffers, and creates the slice directly in its waveform mode. An internal loopback test checks setup, a radio-originated command, RX buffer reflection, normal cleanup, and cleanup after a simulated slice rejection. It sends no PTT. A subsequent live setup-only probe still disrupted the control service, so these changes did not resolve the observed failure. TX remains unadvertised.

An opt-in setup-only probe was run through `SoapySDRUtil --make` on the FLEX-6700. The command completed without an API error and did not request PTT. During a monitored repeat, all 30 ICMP pings succeeded, but TCP port 4992 refused a connection immediately afterward. After recovery, `radio uptime` returned 6507 seconds, greater than the earlier 6404 seconds. The radio therefore did not fully reboot during that check. The observed relays and client interruption are consistent with a SmartSDR control service disruption or restart, but the exact failing step is not isolated.

The live probe entry point was removed from the normal experimental module. It exists only in the loopback test build. No further live waveform setup is planned until the control service failure has been explained.

## Separate transmit paths

The working transmit path in [AetherSDR](https://github.com/aethersdr/AetherSDR) uses a `dax_tx` audio stream and Flex PTT control. DAX TX carries transmit audio, so it cannot be presented as a generic SoapySDR complex IQ TX stream without additional modulation and bandwidth limits.

The [FlexRadio Waveform SDK](https://github.com/flexradio/waveform-sdk) describes complex sample transport through a waveform. The experimental branch follows that path but remains disabled. Any eventual Soapy TX channel must report its actual sample rate and usable RF bandwidth, manage PTT and stream lifetime, and pass a radio test without resets before it is enabled.
