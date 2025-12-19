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
