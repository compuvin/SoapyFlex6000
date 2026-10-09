# FLEX-6700 read-only capability probe — 2026-10-09

This is a snapshot from one FLEX-6700, not a capability table for every FLEX-6000 model. The radio was queried through the SmartSDR TCP API on port 4992. No tuning, transmit, or radio setting commands were sent. Identifiers such as serial number, callsign, MAC address, and IP address are omitted.

## Commands and findings

| Read-only command | Relevant response |
| --- | --- |
| `info` | `model="FLEX-6700"`, `region="USA"`, `num_scu=2`, `num_slice=8`, `num_tx=1`, `atu_present=1` |
| `version` | SmartSDR firmware `3.10.15.38923` at the time of the probe. Driver behavior must not depend on this specific version. |
| `ant list` | `ANT1,ANT2,RX_A,RX_B,XVTR` |
| `radio uptime` | Successful response; useful for a nonmutating connection or status check. |
| `meter list` | Nine meter definitions, listed below. |
| `sub radio all` | Radio status included eight slices, eight panadapters, DAX IQ capacity 16, and oscillator lock state. |
| `sub tx all` | TX status included `tx_rf_power_changes_allowed=1`, `max_power_level=100`, current `rfpower`, TX filter edges, and DAX selection. |
| `sub atu all` | ATU status included `atu_enabled=1`, `memories_enabled=1`, `using_mem=1`, and `status=TUNE_SUCCESSFUL` at the time of the probe. |
| `sub xvtr all` | One inactive, invalid transverter entry; no active transverter range was reported. |
| `sub slice all`, `sub scu all` | Commands succeeded, but no active slice or SCU status yielded frequency limits in this session. |

`radio info` was also tried and returned error `50000016`. The documented information command is simply `info`.

## Frequency ranges

None of the `info`, radio, transmit, slice, SCU, or transverter responses observed in this session supplied RX tuning limits or an explicit set of permitted TX frequency ranges. `region="USA"` identifies the configured region; it is not itself a frequency-range list. No frequency was changed to test acceptance at band edges.

The driver should not derive `getFrequencyRange(RX)` or `getFrequencyRange(TX)` from this snapshot. A future implementation needs a reliable radio-supplied range source or separately justified behavior. The probe does not prove that no other SmartSDR command exposes ranges.

## Meter catalog

| Meter | Radio-reported range and unit | Notes |
| --- | --- | --- |
| `MICPEAK` | -150 to 20 dBFS | Microphone codec peak |
| `MIC` | -150 to 20 dBFS | Microphone codec average |
| `HWALC` | -150 to 20 dBFS | Hardware ALC input |
| `+13.8A` | 10.5 to 15 V | Supply before fuse |
| `+13.8B` | 10.5 to 15 V | Supply after fuse |
| `FWDPWR` | 0 to 53 dBm | Forward RF power |
| `REFPWR` | 0 to 53 dBm | Reflected RF power |
| `SWR` | 1 to 999 SWR | Standing wave ratio |
| `PATEMP` | 0 to 120 °C | PA temperature |

These are meter metadata, not measured values. The existing driver requests `meter list` dynamically, so this snapshot should not become a hard-coded meter list.

## References

- [SmartSDR `info` command](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-info)
- [SmartSDR radio commands](https://github.com/flexradio/smartsdr-api-docs/wiki/TCPIP-radio)
- [SmartSDR TCP/IP API command index](https://github.com/flexradio/smartsdr-api-docs/wiki/SmartSDR-TCPIP-API)
