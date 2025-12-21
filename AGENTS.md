## Project Overview

**fcitx5-unikey** is a Vietnamese input method engine for Fcitx5. It supports multiple input methods (Telex, VNI, VIQR) and character sets. Built with CMake, integrated with Fcitx5 framework.

## Build & Test

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest                    # Run all tests
```

CMake options: `ENABLE_QT` (default: On), `ENABLE_TEST` (default: On), `ENABLE_COVERAGE` (default: Off)

### Testing Workflow

**Quick test workflow:**
```bash
# From anywhere in the project - just run:
scripts/test.sh           # Brief summary mode (recommended)
scripts/test.sh -v        # Verbose mode (show all output)
```

This automatically:
- Detects build directory (current dir or ./build)
- Runs tests with output on failure
- Reruns specific failing cases for easier debugging
- Saves detailed logs to `build/.test-logs/` (won't flood your terminal!)
- Shows just the failure summary + a copy-paste command to rerun with full logs

**Example workflow when a test fails:**
```bash
$ scripts/test.sh
# Shows: "To rerun a specific failing case with full output:"
#        ./bin/testsurroundingtext --case 1

$ cd build
$ ./bin/testsurroundingtext --case 1   # Full debug output!
```

**Manual testing:**
```bash
# From build directory
make -j$(nproc)           # Rebuilds only changed files

# Run all tests
ctest

# Run specific test case
./bin/testsurroundingtext --case 21

# List all test cases
./bin/testsurroundingtext --list-cases
```

**Note:** Don't try `make testsurroundingtext` - there are no individual test targets. Just use `make -j$(nproc)` which rebuilds only what changed.

### Advanced: 2-Pass Test Runner

For detailed logging and advanced debugging, use `scripts/ctest_2pass.sh`:
```bash
bash scripts/ctest_2pass.sh --build-dir build -- -j1
```

This creates detailed logs in `build/.ctest-2pass/` with verbose output from each failing test.

## Architecture

### Core Components

**UnikeyEngine** (`src/unikey-im.{h,cpp}`)
- Main Fcitx5 `InputMethodEngine` implementation
- Creates `UnikeyState` instances per input context (Factory pattern)
- Manages global configuration and UI actions
- Provides keyEvent routing, input method switching, and addon lifecycle

**UnikeyState** (`src/unikey-state.{h,cpp}`)
- Per-context state managing preedit composition, commits, and keystroke history
- Owns `UnikeyInputContext` instance for Vietnamese processing
- Implements "Immediate Commit Mode" for apps with limited preedit support
- Tracks surrounding text reliability with failure thresholds and recovery counters
- Maintains last committed word fallback for unreliable apps
- **Shift+Shift restoration**: Detects two different shift key taps in sequence to restore previous keystrokes for editing
- **Shift+Space**: Commits current composition with a space character
- **BackSpace handling**: Special logic in immediate commit mode to delete from surrounding text or preedit
- **Firefox special support**: Enables immediate commit using internal state tracking even with unreliable surrounding text (bypasses Firefox's buggy Wayland implementation)
- **VNI tone/shape key fallback**: In unreliable mode, can still safely rewrite VNI digits using internal `lastImmediateWord_` history

**Surrounding Text Handler** (`src/unikey-surrounding-text.cpp`)
- Rebuilds Vietnamese composition state from existing text in input field
- Critical for immediate commit mode in Firefox, Chromium, etc.
- Implements reliability detection (2 failures → unreliable, 3 successes → recover)
- Provides fallback mechanisms when apps return empty/stale surrounding text
- **Stale snapshot detection**: Identifies when applications return empty/outdated surrounding text after commits (e.g., Firefox, Chromium)
- **`rebuildStateFromSurrounding()`**: Reconstructs composition state from surrounding text by collecting last contiguous word before cursor
- **`rebuildStateFromLastImmediateWord()`**: Fallback path that uses internally-tracked last committed word when surrounding text is unavailable or unreliable
- **Modify Surrounding Text mode**: Alternative approach that rewrites already-committed text in the input field instead of using preedit
- **Last committed word tracking**: Maintains `lastImmediateWord_` as insurance against stale/buggy surrounding text snapshots
- **Truncation detection**: Recognizes when collected word appears to be a truncated version of expected composition

**Unikey Library** (`unikey/` directory - WARNING: don't touch this)
- Upstream Unikey engine for Vietnamese input processing
- **Core**: `ukengine.cpp` - main Vietnamese composition engine
- **Adapter**: `unikeyinputcontext.{h,cpp}` - wraps UkEngine for Fcitx5
- **Input methods**: `inputproc.{h,cpp}` - Telex, VNI, VIQR mappers
- **Charset support**: `charset.cpp`, `vnconv.h` - 8 output character sets
- **Features**: `mactab.{h,cpp}` (macros), `usrkeymap.{h,cpp}` (custom keymaps)

**Configuration** (`src/unikey-config.h`)
- 13 options: input method, charset, spell check, macro, immediate commit, etc.
- Supports 5 input methods (Telex, VNI, VIQR, SimpleTelex variants)
- Supports 8 output charsets (UTF-8, TCVN3, VNI Win, VIQR, etc.)

**UI Components**
- **Macro Editor** (`macro-editor/` - Qt6-based) - manage Vietnamese macros
- **Keymap Editor** (`keymap-editor/` - Qt6-based) - customize keyboard mappings
- Launched via Fcitx5 ExternalOption from engine UI actions

## Code Layout

```
src/                                    - Fcitx5 integration layer
  ├── unikey-im.{h,cpp}                 - InputMethodEngine implementation
  ├── unikey-state.{h,cpp}              - Per-context composition state
  ├── unikey-surrounding-text.cpp       - Surrounding text rebuild & reliability
  ├── unikey-config.h                   - Configuration options
  └── unikey-{utils,constants,log}.h    - Utilities and helpers

unikey/                                 - Core Vietnamese input engine (upstream Unikey, don't touch it)
  ├── ukengine.{h,cpp}                  - Main composition processor
  ├── unikeyinputcontext.{h,cpp}        - Fcitx5 adapter
  ├── inputproc.{h,cpp}                 - Input method mappers
  ├── charset.{h,cpp}, vnconv.h         - Character set conversions
  ├── mactab.{h,cpp}                    - Macro table management
  └── usrkeymap.{h,cpp}                 - Custom keymap loading

test/                                   - Integration tests
  ├── testunikey.cpp                    - Input composition tests (Telex/VNI)
  ├── testsurroundingtext.cpp           - Surrounding text sync & immediate commit
  └── testkeyhandling.cpp               - Shift restoration, key filtering, preedit

macro-editor/                           - Qt6 macro editor
keymap-editor/                          - Qt6 keymap editor
scripts/                                - Test automation scripts
  ├── test.sh                           - Quick test runner with smart retry
  └── ctest_2pass.sh                    - Advanced 2-pass test runner
```

## Key Concepts

- **Preedit**: Uncommitted text showing Vietnamese transformations as typed
- **Surrounding Text**: Fcitx5 API to access/restore typing state from existing text in input field
- **Immediate Commit Mode**: Direct character commits for apps with limited preedit support
