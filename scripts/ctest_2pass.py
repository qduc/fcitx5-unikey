#!/usr/bin/env python3
"""Run CTest in two passes.

Pass 1: quiet run to quickly identify failing tests.
Pass 2: re-run each failing test individually with full verbosity.

Typical usage:
  python3 scripts/ctest_2pass.py --build-dir build
  python3 scripts/ctest_2pass.py --build-dir build -- -j8
  python3 scripts/ctest_2pass.py --build-dir build --env GLOG_v=2 -- --timeout 120

Notes:
- This script relies on CTest producing Testing/Temporary/LastTestsFailed.log.
- Extra arguments after "--" are passed to BOTH passes.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


@dataclass(frozen=True)
class RunResult:
    returncode: int
    stdout: str


def _is_build_dir(p: Path) -> bool:
    return (p / "CTestTestfile.cmake").is_file() or (p / "CMakeCache.txt").is_file()


def _parse_env_kv(items: Sequence[str]) -> Dict[str, str]:
    env: Dict[str, str] = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"Invalid --env value {item!r}. Expected KEY=VALUE")
        k, v = item.split("=", 1)
        k = k.strip()
        if not k:
            raise ValueError(f"Invalid --env value {item!r}. Empty KEY")
        env[k] = v
    return env


def _run_capture(
    args: Sequence[str],
    *,
    cwd: Path,
    env: Dict[str, str],
) -> RunResult:
    proc = subprocess.run(
        list(args),
        cwd=str(cwd),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return RunResult(proc.returncode, proc.stdout)


def _run_tee(
    args: Sequence[str],
    *,
    cwd: Path,
    env: Dict[str, str],
    log_path: Path,
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as f:
        f.write(f"$ {shlex.join(args)}\n")
        f.flush()
        proc = subprocess.Popen(
            list(args),
            cwd=str(cwd),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert proc.stdout is not None
        for line in proc.stdout:
            sys.stdout.write(line)
            f.write(line)
        return proc.wait()


def _read_failed_tests_file(build_dir: Path) -> List[str]:
    """Parse Testing/Temporary/LastTestsFailed.log.

    Typical format:
      2:testsurroundingtext
      3:testkeyhandling
    """
    p = build_dir / "Testing" / "Temporary" / "LastTestsFailed.log"
    if not p.is_file():
        return []

    names: List[str] = []
    for raw in p.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line:
            continue
        # Common cases:
        # - "2:testname"
        # - "testname"
        if ":" in line:
            _, maybe_name = line.split(":", 1)
            maybe_name = maybe_name.strip()
            if maybe_name:
                names.append(maybe_name)
        else:
            names.append(line)

    # De-duplicate while preserving order.
    seen = set()
    out: List[str] = []
    for n in names:
        if n not in seen:
            out.append(n)
            seen.add(n)
    return out


_FAILED_SECTION_RE = re.compile(r"^The following tests FAILED:")
_FAILED_LINE_RE = re.compile(r"^\s*(\d+)\s*-\s*([^\s].*?)\s*\(Failed\)\s*$")


def _parse_failed_tests_from_output(output: str) -> List[str]:
    in_section = False
    names: List[str] = []
    for line in output.splitlines():
        if _FAILED_SECTION_RE.match(line.strip()):
            in_section = True
            continue
        if not in_section:
            continue
        m = _FAILED_LINE_RE.match(line)
        if m:
            names.append(m.group(2).strip())
        # Stop on empty line after section (ctest usually prints blank line after list)
        elif in_section and not line.strip():
            break

    # De-duplicate while preserving order.
    seen = set()
    out: List[str] = []
    for n in names:
        if n not in seen:
            out.append(n)
            seen.add(n)
    return out


def _exact_test_regex(test_name: str) -> str:
    # Ensure we only match this test name exactly in -R.
    return "^" + re.escape(test_name) + "$"


def _detect_last_case_id(output: str, *, test_name: str) -> Optional[int]:
    """Return the last printed case id for this test, if any.

    Our custom tests print markers like:
      - "testsurroundingtext: Case 20"
      - "testkeyhandling: Case 4 - ..."

    Tests typically abort on the first failure, so the *last* case marker
    is usually the failing case.
    """
    rx = re.compile(rf"\b{re.escape(test_name)}:\s*Case\s+(\d+)\b")
    last: Optional[int] = None
    for m in rx.finditer(output):
        try:
            last = int(m.group(1))
        except ValueError:
            continue
    return last


def _get_ctest_test_command(
    build_dir: Path,
    test_name: str,
    *,
    env: Dict[str, str],
) -> Tuple[List[str], Path]:
    """Return (command_argv, working_dir) for a CTest test.

    Uses: ctest -N -V -R ^name$
    This is more reliable than guessing build/bin paths across generators.
    """
    regex = _exact_test_regex(test_name)
    rr = _run_capture(["ctest", "-N", "-V", "-R", regex], cwd=build_dir, env=env)

    cmd_line: Optional[str] = None
    wd: Optional[Path] = None
    lines = rr.stdout.splitlines()
    for line in lines:
        if line.startswith("Test command:"):
            raw = line.split(":", 1)[1].strip()
            if raw:
                cmd_line = raw
        elif line.startswith("Working Directory:"):
            raw = line.split(":", 1)[1].strip()
            if raw:
                wd = Path(raw)

    # If CTest printed "Test command:" and then the command on the next line.
    if cmd_line is None:
        for i, line in enumerate(lines):
            if line.startswith("Test command:"):
                raw = line.split(":", 1)[1].strip()
                if raw:
                    cmd_line = raw
                else:
                    for j in range(i + 1, min(i + 6, len(lines))):
                        nxt = lines[j].strip()
                        if nxt:
                            cmd_line = nxt
                            break
                break

    if wd is None:
        candidate = build_dir / "test"
        wd = candidate if candidate.is_dir() else build_dir

    if cmd_line is None:
        # Last resort fallback (works for this repo's default layout)
        candidate = build_dir / "bin" / test_name
        cmd = [str(candidate)]
    else:
        cmd = shlex.split(cmd_line)

    return cmd, wd


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="ctest_2pass.py",
        description=(
            "Run CTest in 2 passes: (1) quiet to identify failures; "
            "(2) re-run each failed test individually with full verbosity."
        ),
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="CMake build directory (default: build)",
    )
    parser.add_argument(
        "--pause",
        action="store_true",
        help="Wait for Enter between failed-test reruns (useful for interactive debugging).",
    )
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Set an environment variable for BOTH passes (repeatable).",
    )
    parser.add_argument(
        "--no-second-pass",
        action="store_true",
        help="Only run the first pass and print the failed test list.",
    )
    parser.add_argument(
        "--no-case-pass",
        action="store_true",
        help=(
            "Do not attempt an additional case-only rerun. "
            "(By default, on failure, PASS 2 tries to detect 'Case N' and rerun only that case.)"
        ),
    )
    parser.add_argument(
        "ctest_args",
        nargs=argparse.REMAINDER,
        help="Extra args passed to ctest for BOTH passes. Prefix with '--' to separate.",
    )

    ns = parser.parse_args(list(argv) if argv is not None else None)

    # argparse includes the literal '--' in REMAINDER sometimes, strip it.
    extra = list(ns.ctest_args)
    if extra and extra[0] == "--":
        extra = extra[1:]

    build_dir = Path(ns.build_dir).resolve()
    if not build_dir.exists():
        print(f"Build dir does not exist: {build_dir}", file=sys.stderr)
        return 2
    if not _is_build_dir(build_dir):
        print(
            f"Not a CMake/CTest build dir (missing CTestTestfile.cmake / CMakeCache.txt): {build_dir}",
            file=sys.stderr,
        )
        return 2

    env = dict(os.environ)
    try:
        env.update(_parse_env_kv(ns.env))
    except ValueError as e:
        print(str(e), file=sys.stderr)
        return 2

    logs_dir = build_dir / ".ctest-2pass"
    logs_dir.mkdir(parents=True, exist_ok=True)

    # PASS 1: quiet run
    pass1_cmd = ["ctest", "-Q", *extra]
    print("== PASS 1: running CTest (quiet) to identify failures ==")
    print(f"$ {shlex.join(pass1_cmd)}")
    r1 = _run_capture(pass1_cmd, cwd=build_dir, env=env)
    (logs_dir / "pass1.log").write_text(r1.stdout, encoding="utf-8", errors="replace")

    if r1.returncode == 0:
        print("\nPASS 1 succeeded: no failing tests.")
        return 0

    # Identify failures
    failed = _read_failed_tests_file(build_dir)
    if not failed:
        failed = _parse_failed_tests_from_output(r1.stdout)

    if not failed:
        print("\nPASS 1 failed, but could not determine failed test list.", file=sys.stderr)
        print(f"Saved PASS 1 output to: {logs_dir / 'pass1.log'}", file=sys.stderr)
        print(
            "Tip: check build/Testing/Temporary/LastTest.log, or rerun with: ctest --output-on-failure -VV",
            file=sys.stderr,
        )
        return r1.returncode or 1

    print("\nFailing tests:")
    for i, name in enumerate(failed, 1):
        print(f"  {i}. {name}")

    if ns.no_second_pass:
        print(f"\nSkipping PASS 2 (--no-second-pass). Logs: {logs_dir}")
        return r1.returncode or 1

    # PASS 2: rerun each failed test individually with full verbosity
    overall_rc = 0
    print("\n== PASS 2: rerunning each failed test individually (-VV --output-on-failure) ==")
    for idx, test_name in enumerate(failed, 1):
        if ns.pause:
            input(f"\n[{idx}/{len(failed)}] Press Enter to rerun: {test_name} ")
        else:
            print(f"\n[{idx}/{len(failed)}] Rerunning: {test_name}")

        # Anchor match to the exact test name.
        regex = _exact_test_regex(test_name)
        pass2_cmd = ["ctest", "-R", regex, "-VV", "--output-on-failure", *extra]
        log_path = logs_dir / f"pass2.{test_name}.log"

        rc = _run_tee(pass2_cmd, cwd=build_dir, env=env, log_path=log_path)
        if rc != 0:
            overall_rc = rc
            print(f"\n[FAILED] {test_name} (exit={rc})")

            if not ns.no_case_pass:
                try:
                    out = log_path.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    out = ""
                case_id = _detect_last_case_id(out, test_name=test_name)
                if case_id is None:
                    print("Could not detect failing case id from output; skipping case-only rerun.")
                else:
                    print(f"Detected failing case: {test_name} case {case_id}")
                    try:
                        cmd, wd = _get_ctest_test_command(build_dir, test_name, env=env)
                        case_cmd = [*cmd, "--case", str(case_id)]
                        case_log = logs_dir / f"pass2.{test_name}.case{case_id}.log"
                        print(f"$ {shlex.join(case_cmd)}")
                        rc2 = _run_tee(case_cmd, cwd=wd, env=env, log_path=case_log)
                        if rc2 != 0:
                            print(
                                f"[FAILED] case-only rerun also failed: {test_name} case {case_id} (exit={rc2})"
                            )
                        else:
                            print(f"[OK] case-only rerun passed: {test_name} case {case_id}")
                        print(f"Case log saved: {case_log}")
                    except Exception as e:
                        print(f"Case-only rerun skipped due to error: {e}")
        else:
            print(f"\n[OK] {test_name}")
        print(f"Log saved: {log_path}")

    print(f"\nDone. PASS 1 log: {logs_dir / 'pass1.log'}")
    return overall_rc or 1


if __name__ == "__main__":
    raise SystemExit(main())
