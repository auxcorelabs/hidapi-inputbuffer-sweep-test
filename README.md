# hidapi-sweep-test

A small C program that sweeps `hid_set_num_input_buffers()` across a range of
values on a real HID device and tabulates the return codes. Useful for
discovering the valid-range boundaries on each OS backend.

This test links against the hidapi fork at
[`auxcorelabs/hidapi@feature/input-report-buffer-size`](https://github.com/auxcorelabs/hidapi/tree/feature/input-report-buffer-size),
which provides the `hid_set_num_input_buffers()` API.

## Build

The sweep test is a single C99 source file.

> **Windows users:** skip Steps 1 and 2 below and jump straight to
> [Step 2 → Windows (MSVC) — self-contained](#windows-msvc--self-contained).
> That block is fully standalone — it does arch detection, vcvars setup,
> git clone, cmake, and cl compile in one paste.

### Step 1 — build hidapi from the auxcorelabs branch (macOS / Linux)

You need `git` and `cmake`. On Linux you also need `libusb-1.0-0-dev` (and
`libudev-dev` if you want both backends built; see note under Linux build).

```sh
git clone -b feature/input-report-buffer-size \
    https://github.com/auxcorelabs/hidapi.git hidapi-upstream
cd hidapi-upstream
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build .
cd ../..
```

This produces:

- Linux: `hidapi-upstream/build/src/libusb/libhidapi-libusb.so`
- macOS: `hidapi-upstream/build/src/mac/libhidapi.dylib`

> **Windows users:** skip Step 1. On Windows `cmake` is provided by Visual
> Studio Build Tools and is only on `PATH` inside a Native Tools Command
> Prompt — running the block above from a plain `cmd.exe` fails with
> `'cmake' is not recognized`. The Windows block in Step 2 below is fully
> self-contained (it sets up vcvars, clones the repo, and builds hidapi +
> the sweep test in one go).

### Step 2 — build the sweep test

#### Linux (Debian / Ubuntu / Kali)

Only the **libusb** backend is built here. The `hidraw` backend's
implementation of `hid_set_num_input_buffers()` is validator-only — it checks
the range and returns `0` / `-1`, but cannot actually resize the kernel's
fixed-size hidraw ring buffer (`HIDRAW_BUFFER_SIZE = 64`). Running the sweep
against hidraw tells you nothing about real queue behavior.

```sh
# Install build deps if you haven't already.
# DEBIAN_FRONTEND=noninteractive prevents libc6/debconf postinst prompts
# from hanging the install on fresh or partially-upgraded systems.
sudo DEBIAN_FRONTEND=noninteractive apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake pkg-config libusb-1.0-0-dev git

# Link against the libusb backend
gcc -I hidapi-upstream/hidapi -o sweep_input_buffers hidapi_sweep_test.c \
    hidapi-upstream/build/src/libusb/libhidapi-libusb.so
```

> If the install hits a `libc6` / `libc-dev-bin` version-mismatch error on a
> partially-upgraded system, run:
> `sudo DEBIAN_FRONTEND=noninteractive apt-get -y --fix-broken install`
> and retry the install line.

#### macOS

```sh
gcc -I hidapi-upstream/hidapi -o sweep_input_buffers hidapi_sweep_test.c \
    hidapi-upstream/build/src/mac/libhidapi.dylib \
    -Wl,-rpath,@loader_path/hidapi-upstream/build/src/mac \
    -framework IOKit -framework CoreFoundation
```

> The `-Wl,-rpath,...` flag is required — the dylib's install name is
> `@rpath/libhidapi.0.dylib`, so the binary needs an rpath that points at the
> local build directory. Without it the binary will fail at launch with
> `Library not loaded: @rpath/libhidapi.0.dylib`.

#### Windows (MSVC) — self-contained

Prerequisites:

- **Visual Studio 2022 Build Tools** (or full VS 2022) with the C++ workload
  — provides `cl.exe`, `cmake`, and `vcvarsXX.bat`.
- **Git for Windows** — puts `git` on the system PATH.

The block below autodetects the host architecture from
`%PROCESSOR_ARCHITECTURE%` and invokes the correct `vcvars*.bat` before
running git → cmake → cl. Mixing, e.g. `vcvars64.bat` on an ARM64 host,
produces a `LNK4272: library machine type 'ARM64' conflicts with target
machine type 'x64'` error when linking against the hidapi lib cmake built
for the host.

Paste the block into a plain `cmd.exe`, run from any empty working
directory. It will clone hidapi, build it, fetch/compile the sweep test,
and leave `sweep_input_buffers.exe` + `hidapi.dll` next to each other.

```bat
@echo off
set VS_BUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    call "%VS_BUILD%\vcvarsarm64.bat"
) else (
    call "%VS_BUILD%\vcvars64.bat"
)

REM Clone hidapi (skip if hidapi-upstream already exists)
if not exist hidapi-upstream (
    git clone -b feature/input-report-buffer-size ^
        https://github.com/auxcorelabs/hidapi.git hidapi-upstream
)

REM Build hidapi. cmake picks the host arch from the active vcvars env.
pushd hidapi-upstream
if not exist build mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build . --config RelWithDebInfo
popd

REM Multi-config generators put the lib/dll under RelWithDebInfo\; fall back
REM to the flat path for single-config generators (e.g. NMake Makefiles).
set HIDLIB=hidapi-upstream\build\src\windows\RelWithDebInfo\hidapi.lib
set HIDDLL=hidapi-upstream\build\src\windows\RelWithDebInfo\hidapi.dll
if not exist "%HIDLIB%" set HIDLIB=hidapi-upstream\build\src\windows\hidapi.lib
if not exist "%HIDDLL%" set HIDDLL=hidapi-upstream\build\src\windows\hidapi.dll

REM Build the sweep test. hidapi_sweep_test.c must be in the current directory.
cl.exe /nologo /I hidapi-upstream\hidapi ^
    /Fe:sweep_input_buffers.exe ^
    hidapi_sweep_test.c "%HIDLIB%"
copy /Y "%HIDDLL%" . >nul
```

After the block finishes, launch the binary:

```bat
sweep_input_buffers.exe 0 1025
```

## Usage

All three platforms produce the same binary — `sweep_input_buffers`
(`sweep_input_buffers.exe` on Windows). On Linux it links against the hidapi
libusb backend; on macOS the IOKit backend; on Windows the Win32 HID backend.

```
sweep_input_buffers <start_dec> <end_dec> [--vid <hex> --pid <hex>]
```

| Argument | Required | Format | Meaning |
|---|---|---|---|
| `start_dec` | yes | decimal integer (may be negative) | inclusive sweep start |
| `end_dec` | yes | decimal integer, must be ≥ start | inclusive sweep end |
| `--vid <hex>` | optional | 4-hex-digit uint16 (no `0x` prefix) | USB vendor id |
| `--pid <hex>` | optional | 4-hex-digit uint16 (no `0x` prefix) | USB product id |

Rules:

- `--vid` and `--pid` must be given **together** or **both omitted**.
  Passing only one is an error.
- If both are omitted, the sweep auto-detects and uses the first enumerated
  HID device.
- The sweep range is capped at 1,000,000 values.

## Running

Linux usually requires `sudo` (or a udev rule) to open a HID device:

```sh
# Linux — libusb backend, auto-detect first HID device
sudo LD_LIBRARY_PATH=$PWD/hidapi-upstream/build/src/libusb \
    ./sweep_input_buffers 0 1025
```

macOS and Windows do not need elevated privileges for most HID devices:

```sh
# macOS
./sweep_input_buffers 0 1025
```

```bat
REM Windows
sweep_input_buffers.exe 0 1025
```

## Examples

```sh
# Full sweep, auto-detect the first HID device
./sweep_input_buffers 0 1025

# Zoom in on the Windows HidD_SetNumInputBuffers 512 boundary
./sweep_input_buffers 510 515

# Target a specific device (Beurer PO80 pulse oximeter)
./sweep_input_buffers 0 1025 --vid 28E9 --pid 028A

# Negative-edge check against a specific device
./sweep_input_buffers -5 5 --vid 28E9 --pid 028A
```

Example output:

```
Opened: vid=0x05ac pid=0x027b product=Apple Internal Keyboard (auto-detected)
Sweeping range [0, 1025] (1026 values)...

Results (compact ranges):
  value_range             return_value
  ----------------------  ------------
       0                   -1
       1 .. 1024            0
    1025                   -1
```

A return code of `0` means the value was accepted by the backend; `-1` means
it was rejected (either by hidapi's cap of 1024 or by the OS kernel).
