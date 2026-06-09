#!/usr/bin/env python3
"""
Wrapper around run-clang-tidy with better UX.

This script handles:
- Loading the custom nix-clang-tidy plugin
- Setting up header filters to only check project headers
- Managing parallelism based on available CPUs
- Working around run-clang-tidy's unpatched shebang (NixOS/nixpkgs#214945)
"""

import multiprocessing
import os
import sys
from pathlib import Path


def default_concurrency():
    return min(
        multiprocessing.cpu_count(), int(os.environ.get("NIX_BUILD_CORES", "16"))
    )


def go(
    exe: str,
    plugin_path: Path | None,
    config_file: Path | None,
    compile_commands_json_dir: Path,
    jobs: int,
    paths: list[Path],
    werror: bool,
    fix: bool,
):
    args = [
        # clang-unwrapped ships run-clang-tidy with an unpatched '#!/usr/bin/env python3'
        # shebang (see NixOS/nixpkgs#214945), which fails in sandboxed builds where
        # /usr/bin/env doesn't exist. Invoke through sys.executable to bypass the shebang.
        sys.executable,
        exe,
        "-quiet",
    ]

    if plugin_path is not None:
        args += ["-load", str(plugin_path)]

    if config_file is not None:
        args += ["-config-file", str(config_file)]

    args += [
        "-p",
        str(compile_commands_json_dir),
        "-j",
        str(jobs),
        # Lint all project headers (public include/nix/*/ and private *.hh).
        # Project headers are reached via relative -I flags; system/installed
        # headers are absolute /nix/store/... paths. Match any non-absolute path
        # so the filter works in both monolithic (../src/libfoo/include/...)
        # and per-component (../include/...) build topologies.
        "-header-filter",
        r"^[^/].*\.hh",
        # Exclude generated Bison/Flex headers (parser-tab.hh, lexer-tab.hh)
        "-exclude-header-filter",
        r".-tab\.hh$",
    ]

    if werror:
        args += ["-warnings-as-errors", "*"]
    if fix:
        args += ["-fix"]

    args += ["--"]
    args += [str(p) for p in paths]

    os.execvp(sys.executable, args)


def main():
    import argparse

    ap = argparse.ArgumentParser(description="Run clang-tidy on the Nix codebase")
    ap.add_argument(
        "--jobs",
        "-j",
        type=int,
        default=default_concurrency(),
        help="Parallel linting jobs to run",
    )
    ap.add_argument(
        "--plugin-path",
        type=Path,
        default=None,
        help="Path to the nix-clang-tidy plugin",
    )
    ap.add_argument(
        "--config-file",
        type=Path,
        default=None,
        help="Path to the .clang-tidy config file",
    )
    ap.add_argument(
        "--compdb-path",
        type=Path,
        required=True,
        help="Path to the directory containing the cleaned compilation database",
    )
    ap.add_argument(
        "--werror",
        action="store_true",
        help="Treat warnings as errors",
    )
    ap.add_argument(
        "--fix",
        action="store_true",
        help="Apply fixes for warnings",
    )
    ap.add_argument(
        "--run-clang-tidy-path",
        default="run-clang-tidy",
        help="Path to run-clang-tidy",
    )
    ap.add_argument(
        "paths",
        nargs="*",
        help="Source paths to check",
    )
    args = ap.parse_args()

    go(
        args.run_clang_tidy_path,
        args.plugin_path,
        args.config_file,
        args.compdb_path,
        args.jobs,
        args.paths,
        args.werror,
        args.fix,
    )


if __name__ == "__main__":
    main()
