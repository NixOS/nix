#!/usr/bin/env python3
"""
Single language test runner - runs one test by name.
Usage: test.py <test-name>
Example: test.py eval-okay/arithmetic
"""

import argparse
import difflib
import os
import subprocess
import sys
from pathlib import Path


def diff_and_accept(test_dir: Path, stem: str, got_suffix: str, expected_suffix: str) -> bool:
    """Compare actual output with expected output. Returns True if they match."""
    got = test_dir / f"{stem}.{got_suffix}"
    expected = test_dir / f"{stem}.{expected_suffix}"

    # Absence of expected file indicates empty output expected
    # Use bytes to handle non-UTF-8 content
    if expected.exists():
        expected_content = expected.read_bytes()
    else:
        expected_content = b""

    if got.exists():
        got_content = got.read_bytes()
    else:
        got_content = b""

    if got_content != expected_content:
        print(f"FAIL: evaluation result of {test_dir.name}/{stem} not as expected", file=sys.stderr)
        # Show diff using difflib (cross-platform, no external diff needed)
        try:
            expected_lines = expected_content.decode(errors="replace").splitlines(keepends=True)
            got_lines = got_content.decode(errors="replace").splitlines(keepends=True)
        except Exception:
            expected_lines = []
            got_lines = []
        diff = difflib.unified_diff(
            expected_lines,
            got_lines,
            fromfile=str(expected),
            tofile=str(got),
        )
        sys.stderr.writelines(diff)

        # Update expected if _NIX_TEST_ACCEPT is set
        if os.environ.get("_NIX_TEST_ACCEPT"):
            if got_content:
                expected.write_bytes(got_content)
            elif expected.exists():
                expected.unlink()
        return False

    return True


def postprocess(test_dir: Path, stem: str) -> None:
    """Run postprocess script if it exists."""
    postprocess_script = test_dir / f"{stem}.postprocess.py"
    if postprocess_script.exists():
        test_base = str(test_dir / stem)
        subprocess.run(
            [sys.executable, str(postprocess_script), test_base],
            check=True,
        )


def normalize_paths(file_path: Path, cwd: str) -> None:
    """Replace current working directory with /pwd in file."""
    if file_path.exists():
        content = file_path.read_bytes()
        content = content.replace(cwd.encode(), b"/pwd")
        # On Windows, cwd uses backslashes but nix output may use forward slashes
        cwd_fwd = cwd.replace("\\", "/")
        if cwd_fwd != cwd:
            content = content.replace(cwd_fwd.encode(), b"/pwd")
        file_path.write_bytes(content)


def read_flags(test_dir: Path, stem: str, default: list[str]) -> list[str]:
    """Read custom flags from .flags file or return default."""
    flags_file = test_dir / f"{stem}.flags"
    if flags_file.exists():
        content = flags_file.read_text().strip()
        # Remove comments
        content = content.split("#")[0].strip()
        return content.split()
    return default


def run_parse_fail(nix_cmd: list[str], test_dir: Path, stem: str, cwd: str) -> tuple[bool, bool]:
    """Run a parse-fail test. Returns (exit_code_ok, diff_ok)."""
    print(f"parsing {test_dir.name}/{stem} (should fail)")

    err_file = test_dir / f"{stem}.err"
    nix_file = test_dir / f"{stem}.nix"

    with open(nix_file) as stdin, open(err_file, "w") as stderr:
        result = subprocess.run(
            nix_cmd + ["--parse", "-"],
            stdin=stdin,
            stdout=subprocess.DEVNULL,
            stderr=stderr,
        )

    if result.returncode != 1:
        print(f"FAIL: {test_dir.name}/{stem} shouldn't parse")
        return False, True

    postprocess(test_dir, stem)
    diff_ok = diff_and_accept(test_dir, stem, "err", "err.exp")
    return True, diff_ok


def run_parse_okay(nix_cmd: list[str], test_dir: Path, stem: str, cwd: str) -> tuple[bool, bool]:
    """Run a parse-okay test. Returns (exit_code_ok, diff_ok)."""
    print(f"parsing {test_dir.name}/{stem} (should succeed)")

    out_file = test_dir / f"{stem}.out"
    err_file = test_dir / f"{stem}.err"
    nix_file = test_dir / f"{stem}.nix"

    with open(nix_file) as stdin, open(out_file, "w") as stdout, open(err_file, "w") as stderr:
        result = subprocess.run(
            nix_cmd + ["--parse", "-"],
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

    if result.returncode != 0:
        print(f"FAIL: {test_dir.name}/{stem} should parse")
        return False, True

    normalize_paths(out_file, cwd)
    normalize_paths(err_file, cwd)
    postprocess(test_dir, stem)

    diff_ok = diff_and_accept(test_dir, stem, "out", "exp")
    diff_ok = diff_and_accept(test_dir, stem, "err", "err.exp") and diff_ok
    return True, diff_ok


def run_eval_fail(nix_cmd: list[str], test_dir: Path, stem: str, cwd: str) -> tuple[bool, bool]:
    """Run an eval-fail test. Returns (exit_code_ok, diff_ok)."""
    print(f"evaluating {test_dir.name}/{stem} (should fail)")

    err_file = test_dir / f"{stem}.err"
    nix_file = test_dir / f"{stem}.nix"

    flags = read_flags(test_dir, stem, ["--eval", "--strict", "--show-trace"])

    result = subprocess.run(
        nix_cmd + flags + [str(nix_file)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    # Normalize paths in stderr and write to file (byte-level replace for non-UTF-8 content)
    err_content = result.stderr.replace(cwd.encode(), b"/pwd")
    cwd_fwd = cwd.replace("\\", "/")
    if cwd_fwd != cwd:
        err_content = err_content.replace(cwd_fwd.encode(), b"/pwd")
    err_file.write_bytes(err_content)

    if result.returncode != 1:
        print(f"FAIL: {test_dir.name}/{stem} shouldn't evaluate")
        return False, True

    postprocess(test_dir, stem)
    diff_ok = diff_and_accept(test_dir, stem, "err", "err.exp")
    return True, diff_ok


def run_eval_okay(nix_cmd: list[str], test_dir: Path, stem: str, cwd: str, is_windows_host: bool) -> tuple[bool, bool]:
    """Run an eval-okay test. Returns (exit_code_ok, diff_ok)."""
    print(f"evaluating {test_dir.name}/{stem} (should succeed)")

    nix_file = test_dir / f"{stem}.nix"
    exp_xml = test_dir / f"{stem}.exp.xml"
    exp_disabled = test_dir / f"{stem}.exp-disabled"

    if exp_xml.exists():
        # XML output test
        out_file = test_dir / f"{stem}.out.xml"
        result = subprocess.run(
            nix_cmd + ["--eval", "--xml", "--no-location", "--strict", str(nix_file)],
            stdout=open(out_file, "w"),
            stderr=subprocess.DEVNULL,
        )

        if result.returncode != 0:
            print(f"FAIL: {test_dir.name}/{stem} should evaluate")
            return False, True

        postprocess(test_dir, stem)
        diff_ok = diff_and_accept(test_dir, stem, "out.xml", "exp.xml")
        return True, diff_ok

    elif exp_disabled.exists():
        # Test is disabled, skip it
        print(f"SKIP: {test_dir.name}/{stem} is disabled")
        return True, True

    else:
        # Normal evaluation test
        out_file = test_dir / f"{stem}.out"
        err_file = test_dir / f"{stem}.err"

        flags = read_flags(test_dir, stem, [])

        env = os.environ.copy()
        pathsep = ";" if is_windows_host else ":"
        env["NIX_PATH"] = f"lang/dir3{pathsep}lang/dir4"
        env["HOME"] = "/fake-home"
        if is_windows_host:
            env["USERPROFILE"] = "C:\\fake-home"

        with open(out_file, "w") as stdout, open(err_file, "w") as stderr:
            result = subprocess.run(
                nix_cmd + flags + ["--eval", "--strict", str(nix_file)],
                stdout=stdout,
                stderr=stderr,
                env=env,
            )

        if result.returncode != 0:
            print(f"FAIL: {test_dir.name}/{stem} should evaluate")
            return False, True

        normalize_paths(out_file, cwd)
        normalize_paths(err_file, cwd)
        postprocess(test_dir, stem)

        diff_ok = diff_and_accept(test_dir, stem, "out", "exp")
        diff_ok = diff_and_accept(test_dir, stem, "err", "err.exp") and diff_ok
        return True, diff_ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a single language test")
    parser.add_argument("test_name", help="Name of the test (e.g., eval-okay/arithmetic)")
    args = parser.parse_args()

    test_name = args.test_name

    # Parse subdir/stem from test name
    if "/" in test_name:
        category, stem = test_name.split("/", 1)
    else:
        # Backwards compat: parse old-style flat names (e.g., eval-okay-arithmetic)
        for prefix in ("parse-fail-", "parse-okay-", "eval-fail-", "eval-okay-"):
            if test_name.startswith(prefix):
                category = prefix.rstrip("-")
                stem = test_name[len(prefix):]
                break
        else:
            print(f"Unknown test type: {test_name}", file=sys.stderr)
            return 1

    test_dir = Path("lang") / category
    cwd = os.getcwd()

    # Determine if the host target is Windows (cross-compilation or native)
    host_os = os.environ.get("_NIX_TEST_HOST_OS", "")
    is_windows_host = host_os == "windows" or sys.platform == "win32"

    # Get nix-instantiate path from NIX_BIN_DIR
    nix_bin_dir = os.environ.get("NIX_BIN_DIR")
    exe_suffix = ".exe" if is_windows_host else ""
    if nix_bin_dir:
        nix_instantiate = str(Path(nix_bin_dir) / f"nix-instantiate{exe_suffix}")
    else:
        nix_instantiate = f"nix-instantiate{exe_suffix}"

    # When cross-compiling, prepend the exe wrapper (e.g. Wine) to all
    # nix-instantiate invocations. The wrapper path is passed from meson.build.
    exe_wrapper = os.environ.get("_NIX_TEST_EXE_WRAPPER", "")
    nix_cmd: list[str] = []
    if exe_wrapper and is_windows_host and sys.platform != "win32":
        nix_cmd = [exe_wrapper, nix_instantiate]

        # Build WINEPATH so Wine can find DLLs. Collect directories containing
        # .dll files from the build tree, plus any paths in LINK_DLL_FOLDERS
        # (set by nix cross-compilation dev shells).
        winepath_dirs: list[str] = []
        if nix_bin_dir:
            src_dir = Path(nix_bin_dir).parent
            if src_dir.is_dir():
                for child in src_dir.iterdir():
                    if child.is_dir() and any(child.glob("*.dll")):
                        winepath_dirs.append(str(child))
        link_dll_folders = os.environ.get("LINK_DLL_FOLDERS", "")
        if link_dll_folders:
            winepath_dirs.extend(link_dll_folders.split(":"))
        if winepath_dirs:
            existing = os.environ.get("WINEPATH", "")
            sep = ";" if existing else ""
            os.environ["WINEPATH"] = existing + sep + ";".join(winepath_dirs)
    else:
        nix_cmd = [nix_instantiate]

    # Set up environment
    os.environ["TEST_VAR"] = "foo"  # for eval-okay/getenv.nix
    os.environ["NIX_REMOTE"] = "dummy://"
    os.environ["NIX_STORE_DIR"] = "/nix/store"
    # Enable experimental features and settings that the lang tests need
    os.environ["NIX_CONFIG"] = "experimental-features = nix-command flakes fetch-tree\nshow-trace = true"

    if category == "parse-fail":
        exit_ok, diff_ok = run_parse_fail(nix_cmd, test_dir, stem, cwd)
    elif category == "parse-okay":
        exit_ok, diff_ok = run_parse_okay(nix_cmd, test_dir, stem, cwd)
    elif category == "eval-fail":
        exit_ok, diff_ok = run_eval_fail(nix_cmd, test_dir, stem, cwd)
    elif category == "eval-okay":
        exit_ok, diff_ok = run_eval_okay(nix_cmd, test_dir, stem, cwd, is_windows_host)
    else:
        print(f"Unknown test type: {test_name}", file=sys.stderr)
        return 1

    # Handle _NIX_TEST_ACCEPT mode
    if os.environ.get("_NIX_TEST_ACCEPT"):
        if not diff_ok:
            print(
                "Output did not match, but accepted output as the persisted expected output.",
                file=sys.stderr,
            )
            print("That means the next time the tests are run, they should pass.", file=sys.stderr)
        if not exit_ok:
            return 1
        return 77  # Skip exit code for meson

    if not exit_ok or not diff_ok:
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
