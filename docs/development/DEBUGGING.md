# Debugging SDR9700

Use the debug build when you need debug symbols or debugger-friendly behavior:

```bash
make debug
```

Launch the debug executable directly on Linux:

```bash
./src/build/bin/SDR9700
```

On macOS, the executable is inside the application bundle:

```bash
./src/build/bin/SDR9700.app/Contents/MacOS/SDR9700
```

`make debug` cleans and reconfigures `src/build` with CMake's `Debug` build
type. Debug builds default to `--log=all` when no explicit log option is
supplied.

Runtime logging can also be enabled in release builds. On Linux:

```bash
./src/build/bin/SDR9700 --log=radio,udp,ci-v
./src/build/bin/SDR9700 --log=all --log-file=/tmp/sdr9700.log
```

On macOS, use the corresponding bundled executable:

```bash
./src/build/bin/SDR9700.app/Contents/MacOS/SDR9700 --log=radio,udp,ci-v
./src/build/bin/SDR9700.app/Contents/MacOS/SDR9700 --log=all \
  --log-file=/tmp/sdr9700.log
```

Supported logging categories:

- `all`
- `audio`
- `audioconverter`
- `ci-v`
- `gui`
- `icom-rc-28`
- `radio`
- `spectrumScope`
- `system`
- `udp`
- `waterfall`

`--log-file=<path>` appends the CLI-selected formatted diagnostics to a file
in addition to writing them to the console.
Each line includes a conventional severity name: `DEBUG`, `INFO`, `WARN`,
`ERROR`, or `FATAL`.
