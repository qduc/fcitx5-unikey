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

Testing helper: the project includes a small 2-pass test runner that makes
debugging failing integration tests easier. It lives at `scripts/ctest_2pass.py`
and will automatically rerun failing test binaries with verbose output, and
—when possible — detect and rerun only a failing "case" within a test binary
(the test binaries expose `--list-cases` and `--case N`).

Quick workflow
1. Build and run tests:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
# Run quick quiet pass and then verbose per-test reruns. Extra args after "--"
# are forwarded to ctest (e.g. parallelism).
python3 ../scripts/ctest_2pass.py --build-dir "$PWD" -- -j1
```

2. Inspect per-test logs saved under the build dir:

```
build/.ctest-2pass/pass1.log                 # PASS 1 (ctest -Q) output
build/.ctest-2pass/pass2.<testname>.log      # PASS 2 verbose rerun for a test
build/.ctest-2pass/pass2.<testname>.caseN.log # Case-only rerun (when detected)
```

Listing and running individual cases

Some integration test binaries include many numbered "cases" (small
scenarios) inside a single executable. Two updated test binaries in this
repository support convenient selection:

- `build/bin/testsurroundingtext --list-cases` — list available case IDs.
- `build/bin/testsurroundingtext --case 12` — run only case 12.

Same for `testkeyhandling`:

- `build/bin/testkeyhandling --list-cases`
- `build/bin/testkeyhandling --case 5`

This is useful for quickly reproducing and debugging a single failing
scenario without rerunning the entire suite.

How the 2-pass runner uses cases

- PASS 1: runs `ctest -Q` to quickly discover failing tests.
- PASS 2: reruns each failing test with `ctest -R <name> -VV --output-on-failure`.
	- If the test binary prints a marker like `testname: Case N`, the runner
		will detect the last such marker and automatically rerun the test binary
		directly with `--case N`, saving a dedicated per-case log. This produces
		a much smaller, focused log for debugging.
- To skip the automated case-only rerun, pass `--no-case-pass` to the runner.

Troubleshooting

- If `ctest_2pass.py` cannot find the build dir, ensure you ran `cmake ..` in
	that directory and are passing `--build-dir` correctly.
- If a case-only rerun is not produced, the binary may not print the
	`testname: Case N` marker (or the failing run aborted before printing it).
	You can manually list cases and invoke `--case N`.
- Per-case logs and PASS 2 full logs are stored under `build/.ctest-2pass/`.

Example: reproduce one failing case locally

```bash
# show cases
build/bin/testsurroundingtext --list-cases

# run just case 20 for fast debugging
build/bin/testsurroundingtext --case 20
```

If you want to use the original behavior (no per-case detection), just run
`ctest -VV --output-on-failure` from the build dir or pass
`--no-case-pass` to `scripts/ctest_2pass.py`.

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
