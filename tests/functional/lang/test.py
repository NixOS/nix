#!/usr/bin/env python3
"""
Single language test runner - runs one test by name.
Usage: test.py <test-name>
Example: test.py eval-okay-arithmetic
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


def diff_and_accept(test_name: str, got_suffix: str, expected_suffix: str) -> bool:
    """Compare actual output with expected output. Returns True if they match."""
    got = Path("lang") / f"{test_name}.{got_suffix}"
    expected = Path("lang") / f"{test_name}.{expected_suffix}"

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
        print(f"FAIL: evaluation result of {test_name} not as expected", file=sys.stderr)
        # Show diff
        subprocess.run(
            ["diff", "--color=always", "--unified", str(expected), str(got)],
            check=False,
        )

        # Update expected if _NIX_TEST_ACCEPT is set
        if os.environ.get("_NIX_TEST_ACCEPT"):
            if got_content:
                expected.write_bytes(got_content)
            elif expected.exists():
                expected.unlink()
        return False

    return True


def postprocess(test_name: str) -> None:
    """Run postprocess script if it exists."""
    postprocess_script = Path("lang") / f"{test_name}.postprocess"
    if postprocess_script.exists():
        subprocess.run(
            ["bash", str(postprocess_script), f"lang/{test_name}"],
            check=True,
        )


def normalize_paths(file_path: Path, cwd: str) -> None:
    """Replace current working directory with /pwd in file."""
    if file_path.exists():
        content = file_path.read_bytes()
        content = content.replace(cwd.encode(), b"/pwd")
        file_path.write_bytes(content)


def read_flags(test_name: str, default: list[str]) -> list[str]:
    """Read custom flags from .flags file or return default."""
    flags_file = Path("lang") / f"{test_name}.flags"
    if flags_file.exists():
        content = flags_file.read_text().strip()
        # Remove comments
        content = content.split("#")[0].strip()
        return content.split()
    return default


def run_parse_fail(nix_instantiate: str, test_name: str, cwd: str) -> tuple[bool, bool]:
    """Run a parse-fail test. Returns (exit_code_ok, diff_ok)."""
    print(f"parsing {test_name} (should fail)")

    err_file = Path("lang") / f"{test_name}.err"
    nix_file = Path("lang") / f"{test_name}.nix"

    with open(nix_file) as stdin, open(err_file, "w") as stderr:
        result = subprocess.run(
            [nix_instantiate, "--parse", "-"],
            stdin=stdin,
            stdout=subprocess.DEVNULL,
            stderr=stderr,
        )

    if result.returncode != 1:
        print(f"FAIL: {test_name} shouldn't parse")
        return False, True

    postprocess(test_name)
    diff_ok = diff_and_accept(test_name, "err", "err.exp")
    return True, diff_ok


def run_parse_okay(nix_instantiate: str, test_name: str, cwd: str) -> tuple[bool, bool]:
    """Run a parse-okay test. Returns (exit_code_ok, diff_ok)."""
    print(f"parsing {test_name} (should succeed)")

    out_file = Path("lang") / f"{test_name}.out"
    err_file = Path("lang") / f"{test_name}.err"
    nix_file = Path("lang") / f"{test_name}.nix"

    with open(nix_file) as stdin, open(out_file, "w") as stdout, open(err_file, "w") as stderr:
        result = subprocess.run(
            [nix_instantiate, "--parse", "-"],
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

    if result.returncode != 0:
        print(f"FAIL: {test_name} should parse")
        return False, True

    normalize_paths(out_file, cwd)
    normalize_paths(err_file, cwd)
    postprocess(test_name)

    diff_ok = diff_and_accept(test_name, "out", "exp")
    diff_ok = diff_and_accept(test_name, "err", "err.exp") and diff_ok
    return True, diff_ok


def run_eval_fail(nix_instantiate: str, test_name: str, cwd: str) -> tuple[bool, bool]:
    """Run an eval-fail test. Returns (exit_code_ok, diff_ok)."""
    print(f"evaluating {test_name} (should fail)")

    err_file = Path("lang") / f"{test_name}.err"
    nix_file = Path("lang") / f"{test_name}.nix"

    flags = read_flags(test_name, ["--eval", "--strict", "--show-trace"])

    result = subprocess.run(
        [nix_instantiate] + flags + [str(nix_file)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    # Normalize paths in stderr and write to file (byte-level replace for non-UTF-8 content)
    err_content = result.stderr.replace(cwd.encode(), b"/pwd")
    err_file.write_bytes(err_content)

    if result.returncode != 1:
        print(f"FAIL: {test_name} shouldn't evaluate")
        return False, True

    postprocess(test_name)
    diff_ok = diff_and_accept(test_name, "err", "err.exp")
    return True, diff_ok


def run_eval_okay(nix_instantiate: str, test_name: str, cwd: str) -> tuple[bool, bool]:
    """Run an eval-okay test. Returns (exit_code_ok, diff_ok)."""
    print(f"evaluating {test_name} (should succeed)")

    nix_file = Path("lang") / f"{test_name}.nix"
    exp_xml = Path("lang") / f"{test_name}.exp.xml"
    exp_disabled = Path("lang") / f"{test_name}.exp-disabled"

    if exp_xml.exists():
        # XML output test
        out_file = Path("lang") / f"{test_name}.out.xml"
        result = subprocess.run(
            [nix_instantiate, "--eval", "--xml", "--no-location", "--strict", str(nix_file)],
            stdout=open(out_file, "w"),
            stderr=subprocess.DEVNULL,
        )

        if result.returncode != 0:
            print(f"FAIL: {test_name} should evaluate")
            return False, True

        postprocess(test_name)
        diff_ok = diff_and_accept(test_name, "out.xml", "exp.xml")
        return True, diff_ok

    elif exp_disabled.exists():
        # Test is disabled, skip it
        print(f"SKIP: {test_name} is disabled")
        return True, True

    else:
        # Normal evaluation test
        out_file = Path("lang") / f"{test_name}.out"
        err_file = Path("lang") / f"{test_name}.err"

        flags = read_flags(test_name, [])

        env = os.environ.copy()
        env["NIX_PATH"] = "lang/dir3:lang/dir4"
        env["HOME"] = "/fake-home"

        with open(out_file, "w") as stdout, open(err_file, "w") as stderr:
            result = subprocess.run(
                [nix_instantiate] + flags + ["--eval", "--strict", str(nix_file)],
                stdout=stdout,
                stderr=stderr,
                env=env,
            )

        if result.returncode != 0:
            print(f"FAIL: {test_name} should evaluate")
            return False, True

        normalize_paths(out_file, cwd)
        normalize_paths(err_file, cwd)
        postprocess(test_name)

        diff_ok = diff_and_accept(test_name, "out", "exp")
        diff_ok = diff_and_accept(test_name, "err", "err.exp") and diff_ok
        return True, diff_ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a single language test")
    parser.add_argument("test_name", help="Name of the test (e.g., eval-okay-arithmetic)")
    args = parser.parse_args()

    test_name = args.test_name
    cwd = os.getcwd()

    # Get nix-instantiate path from NIX_BIN_DIR
    nix_bin_dir = os.environ.get("NIX_BIN_DIR")
    if nix_bin_dir:
        nix_instantiate = str(Path(nix_bin_dir) / "nix-instantiate")
    else:
        nix_instantiate = "nix-instantiate"

    # Set up environment
    os.environ["TEST_VAR"] = "foo"  # for eval-okay-getenv.nix
    os.environ["NIX_REMOTE"] = "dummy://"
    os.environ["NIX_STORE_DIR"] = "/nix/store"
    # Enable experimental features and settings that the lang tests need
    os.environ["NIX_CONFIG"] = "experimental-features = nix-command flakes fetch-tree\nshow-trace = true"

    if test_name.startswith("parse-fail-"):
        exit_ok, diff_ok = run_parse_fail(nix_instantiate, test_name, cwd)
    elif test_name.startswith("parse-okay-"):
        exit_ok, diff_ok = run_parse_okay(nix_instantiate, test_name, cwd)
    elif test_name.startswith("eval-fail-"):
        exit_ok, diff_ok = run_eval_fail(nix_instantiate, test_name, cwd)
    elif test_name.startswith("eval-okay-"):
        exit_ok, diff_ok = run_eval_okay(nix_instantiate, test_name, cwd)
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
