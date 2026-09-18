# IC-9700 CI-V VFO Command Scope Notes

This note tracks which SDR9700-facing controls appear to be VFO-specific and
which appear to be global on the IC-9700. Items marked as inferred should be
validated with live CI-V logs before being treated as final behavior.

| Control / CI-V area | Command(s) | Scope | SDR9700 implication |
|---|---:|---|---|
| VFO A/B / MAIN/SUB select | `07 D0`, `07 D1` | VFO selection state | Used to target MAIN or SUB. |
| Frequency | `03`, `05` | VFO-specific by selected MAIN/SUB | Select target VFO, set/read frequency, then restore TX side if needed. |
| Band button | wraps `05` | VFO-specific by selected MAIN/SUB | Same handling as frequency. |
| Mode | `04`, `06` | VFO-specific by selected MAIN/SUB | Select target VFO, set/read mode, then restore TX side if needed. |
| MAIN-only selected/unselected frequency | `25 00`, `25 01` | MAIN VFO A/B only, not SUB | Do not use for VFO B/SUB. The CI-V manual says SUB frequency cannot be set this way. |
| MAIN-only selected/unselected mode | `26 00`, `26 01` | MAIN VFO A/B only, not SUB | Do not use for VFO B/SUB. The CI-V manual says SUB mode/filter cannot be set this way. |
| VOL / AF gain | `14 01` | Selected-side/VFO-specific | SDR9700 targets and caches the value independently for MAIN and SUB. |
| RF gain | `14 02` | Selected-side/VFO-specific | SDR9700 targets and caches the value independently for MAIN and SUB. |
| SQL | `14 03` | Selected-side/VFO-specific | SDR9700 targets and caches the value independently for MAIN and SUB. |
| S-meter | `15 02` | Selected receiver read | SDR9700 alternates receiver-targeted polling while dualwatch is active so both receiver panels remain current. |
| Scope / Spectrum Scope | `27 xx`, especially `27 12` | Receiver-selectable | The displayed scope follows the operator-selected MAIN or SUB receiver. Receiver changes are coordinated with scope routing so delayed data cannot silently retarget the display. |
| TX PWR | `14 0A` | Unconfirmed: likely transmitter/global or per-band radio state | Current code treats it global. Validate before making it VFO-specific. |
| MIC gain | `14 0B` | Global TX audio path | Keep global. |
| PTT | transceiver status | Global transmitter state | GUI PTT is momentary. TX side determines which VFO transmits. |
| AGC | `16 12` | Selected-side/VFO-specific | SDR9700 targets and caches the value independently for MAIN and SUB. |
| ATT / PRE | `11`, `16 02` | Selected-side/band-specific | SDR9700 targets and caches these values independently for MAIN and SUB. |
| NR / NB / Notch | `16 40`, `16 22`, `16 41` | Selected-side/DSP-chain-specific | SDR9700 targets and caches these values independently for MAIN and SUB. |
| Split / offset / tone / DCS | `0F`, `0C`/`0D`, `16 5D`, `1B xx` | VFO/channel operating state, not whole app | SDR9700 keeps receiver-scoped values separate where the radio exposes a usable readback path. |
| Dualwatch | `16 59` | Global radio mode | Keep global. |
| Satellite mode | `16 5A` | Global radio mode | Keep global. |

## Current Code References

- Command definitions: `src/radio/RadioCapabilities.h`
- Explicit VFO frequency/mode targeting: `src/backend/RadioBackend.cpp`
- Receiver-targeted control, polling, and MAIN/SUB synchronization: `src/backend/RadioBackend.cpp`
- Parsed-value routing and receiver-specific cache state: `src/core/CachingQueue.cpp`
