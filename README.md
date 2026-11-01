# qtdskbrowser

A Qt6 desktop application for browsing Amstrad CPC `.dsk` disk images. It
reads the AMSDOS directory (grouped by user number), lets you open `.BAS`
files directly from the disk, and detokenises/decrypts them (including
AMSDOS "protected" BASIC) for display in an authentic CPC-style view using
the original character ROM font.

![qtdskbrowser screenshot](resources/screenshot.png)

## Requirements

- A C++17 compiler (GCC or Clang)
- [CMake](https://cmake.org/) 3.16 or newer
- Qt 6, with the **Widgets** module

On Debian/Ubuntu, the required packages can be installed with:

```bash
sudo apt install build-essential cmake qt6-base-dev
```

## Building

From the project root:

```bash
cmake -B build -S .
cmake --build build
```

The resulting executable is `build/qtdskbrowser`.

## Running

```bash
./build/qtdskbrowser
```

## Installing

To install the compiled binary to `/usr/local/bin`:

```bash
sudo cmake --install build
```

## License notes

The character ROM font data in `cpcglyphs.h` is the Amstrad CPC's own
bitmap font (not a modern recreation); see
[`resources/LICENSE-cpc464.txt`](resources/LICENSE-cpc464.txt) for its
source and licensing details.
