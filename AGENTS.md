## Project Overview

**fcitx5-unikey** is a Vietnamese input method engine for Fcitx5. It supports multiple input methods (Telex, VNI, VIQR) and character sets. Built with CMake, integrated with Fcitx5 framework.

## Build & Test

```bash
mkdir build && cd build
cmake ..
make
ctest                    # Run tests
```

CMake options: `ENABLE_QT` (default: On), `ENABLE_TEST` (default: On), `ENABLE_COVERAGE` (default: Off)

## Architecture

**UnikeyEngine** (`src/unikey-im.{h,cpp}`)
- Main Fcitx5 `InputMethodEngine` implementation
- Creates `UnikeyState` instances per input context

**UnikeyState** (`src/unikey-state.{h,cpp}`)
- Per-context state managing preedit, commits, and surrounding text
- Supports "Immediate Commit Mode" for apps with poor preedit support

**Unikey Library** (`unikey/` directory)
- Upstream Unikey engine for Vietnamese input processing
- Key files: `ukengine.cpp`, `unikeyinputcontext.{h,cpp}`

**Configuration** (`src/unikey-config.h`)
- Input methods, charsets, and options (spell check, macro, immediate commit, etc.)

## Code Layout

```
src/                     - Fcitx5 integration layer
unikey/                  - Core Vietnamese input engine (upstream)
test/testunikey.cpp      - Integration tests using Fcitx5::TestFrontend
macro-editor/            - Qt macro editor
keymap-editor/           - Qt keymap editor
```

## Key Concepts

- **Preedit**: Uncommitted text showing Vietnamese transformations as typed
- **Surrounding Text**: Fcitx5 API to access/restore typing state from existing text in input field
- **Immediate Commit Mode**: Direct character commits for apps with limited preedit support
