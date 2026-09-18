# SDR9700 Third-Party Licenses

SDR9700 incorporates or references the third-party work listed below. Each
component retains its original license.

This file is an attribution and license inventory.

## Bundled Icons

### Tabler Icons

The control-lock indicators use modified `lock` and `lock-open` SVG icons from
Tabler Icons version 3.46.0.

- Project: <https://github.com/tabler/tabler-icons>
- Copyright: Copyright (c) 2020-2026 Paweł Kuna
- License: MIT

```text
MIT License

Copyright (c) 2020-2026 Paweł Kuna

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Active Protocol Attribution

### AetherSDR Project

SDR9700 benefited from AetherSDR as a source of design inspiration, operating
experience, and behavior comparison while SDR9700 was developed as its own
IC-9700-focused application. SDR9700's spectrum heat-map and S-meter color
palettes use the corresponding AetherSDR palette definitions.

- Reference project: AetherSDR
- Reference URL: <https://github.com/aethersdr/AetherSDR>
- License: GPL-3.0

### Data Decoder Research References

The receive-only AX.25 decoder was written as original SDR9700 code. AetherSDR,
Dire Wolf, and libmodem were reviewed to understand common Bell 202 and AX.25
decoder designs; no source code from these projects is incorporated into
SDR9700's decoder.

- Reference project: Dire Wolf
- Reference URL: <https://github.com/wb2osz/direwolf>
- License: GPL-2.0-or-later

- Reference project: libmodem
- Reference URL: <https://github.com/iontodirel/libmodem>
- License: MIT

### Icom IC-9700 LAN Protocol Research

The IC-9700 LAN protocol stack in `src/radio/`, `src/audio/`, `src/core/`, and
`src/backend/` is maintained as SDR9700 project code. Protocol behavior and
compatibility were informed by public IC-9700 behavior, local testing, Icom
documentation, and prior open-source Icom LAN client work.

- Reference project: wfview
- Reference URL: <https://gitlab.com/eliggett/wfview>
- Copyright: Copyright (C) 2021-2025 Phil Taylor M0VSE and the wfview contributors
- Authors: Phil Taylor M0VSE, Elliott Liggett, and contributors
- License: GPL-2.0-or-later

- Reference project: radio-webop
- Reference URL: <https://github.com/Dreikor17/radio-webop>
- Copyright: Copyright (C) 2026 Dreikor17
- License: AGPL-3.0-only

## Build-Time/System Dependencies

The application links against system-provided libraries when available. These
are not vendored in this repository.

- Qt 6: Core, Widgets, Network, and Multimedia.
- XKB common: required by Qt GUI platform dependency discovery.
- Opus: required codec dependency discovered through `pkg-config`.
- SpeexDSP: required resampler dependency discovered through `pkg-config`.
- Eigen3: required for audio sample format conversion in `src/audio/AudioConverter`.
- HIDAPI (optional): used by `Rc28Manager` for Icom RC-28 HID control; linked
  when `libhidapi-hidraw` or `libhidapi` is detected at build time via
  `pkg-config`. Enables the `HAVE_HIDAPI` compile definition.

## Manual Material Not Distributed As SDR9700 Code

The `resources/manuals/` directory contains local IC-9700 manuals and related
research material for contributors and AI agents. It is not built into SDR9700
and must not be treated as SDR9700 source.

Imported notes, old docs, tests, and packaging work must be validated,
rewritten, or removed before they become shipping SDR9700 documentation or
code.
