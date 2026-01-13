# fcitx5-unikey

A Vietnamese input method engine for [Fcitx 5](https://github.com/fcitx/fcitx5), based on the Unikey engine.

## Origin and Rationale

This project is a fork of the official [fcitx5-unikey](https://github.com/fcitx/fcitx5-unikey).

The primary motivation for this fork is to implement **Immediate Commit Mode**. In the official version, text remains in "preedit" mode until committed, which often prevents browsers (like Firefox and Chromium) from accurately reading the input field for URL bar autocompletion and search suggestions.

By committing characters immediately to the application while still maintaining the Vietnamese composition state internally, this fork ensures that browser suggestions work seamlessly as you type.

## Features

- **Multiple Input Methods**: Supports Telex, VNI, VIQR, and Simple Telex variants.
- **Modern Vietnamese Support**: Built-in spell checking, modern tone placement, and macro support.
- **Rich Integration**:
    - Per-context state management.
    - **Surrounding Text** support: Rebuilds composition state from existing text, critical for apps like Firefox and Chromium.
    - **Immediate Commit Mode**: Direct character commits for applications with limited preedit support.
- **Interactive Editors**: Includes Qt6-based editors for Macros and Custom Keymaps.
- **Smart Shortcuts**:
    - `Shift` + `Shift`: Restoration of previous keystrokes for quick editing.
    - `Shift` + `Space`: Commit current composition with a space.

## Requirements

This project requires a modern Linux environment with Fcitx 5 development headers.

- **Fcitx 5**: Version 5.1.13 or newer is recommended.
- **C++ Standard**: C++20 (GCC 12+ or Clang 14+).
- **Qt 6**: For macro and keymap editors (optional, can be disabled).

### Verified Distributions
- **Arch Linux**: Builds successfully with latest `fcitx5` (5.1.17+).
- **Debian Testing/Unstable (Sid)**: Verified to work with Fcitx 5 (5.1.17).
- **Fedora**: Supported.
- *Note: Ubuntu 24.04 and earlier may require manual backporting of Fcitx 5 as they ship older versions (5.1.7).*

## Build and Install

> [!IMPORTANT]
> This package provides the same components as the official `fcitx5-unikey`. To avoid file conflicts and ensure correct behavior, you **must uninstall the official package** before installing this fork.

### Dependencies

On Arch Linux:
```bash
sudo pacman -S base-devel cmake extra-cmake-modules gettext fcitx5 fcitx5-qt qt6-base
```

On Debian/Ubuntu (Requires fcitx5 >= 5.1.13):
```bash
sudo apt install build-essential cmake extra-cmake-modules gettext libfcitx5core-dev libfcitx5utils-dev fcitx5-modules-dev libfcitx5-qt6-dev qt6-base-dev
```

On Fedora:
```bash
sudo dnf install gcc-c++ cmake extra-cmake-modules gettext fcitx5-devel fcitx5-qt-devel qt6-qtbase-devel
```

### Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Build Options

- `-DENABLE_QT=On/Off`: Enable/Disable Qt6-based editors (Default: `On`).
- `-DENABLE_TEST=On/Off`: Build unit tests (Default: `On`).

## Development

To run tests:
```bash
cd build
ctest --output-on-failure
```

For more detailed development information, see [AGENTS.md](AGENTS.md).

## License

This project is released under **GPLv2+**.
The core Unikey engine included in the `unikey/` directory may have its own licensing terms; please refer to the files in that directory for details.
