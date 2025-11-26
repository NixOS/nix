#!/usr/bin/env python3
"""
Builds all generated files before running clang-tidy.

clang-tidy needs all source files and headers to exist before it can
analyze the code. This script queries Ninja for all custom command
targets that generate files needed for analysis and builds them.

Generated files include:
- .gen.hh: Embedded file headers (SQL schemas, Nix expressions, etc.)
- .gen.inc: Generated include files
- lexer-tab.cc, parser-tab.cc: Flex/Bison generated parsers
- Perl XS bindings, Kaitai parsers, and other generated sources

See: https://github.com/mesonbuild/meson/issues/12817
"""

import subprocess


def get_targets_of_rule(build_root: str, rule_name: str) -> list[str]:
    return (
        subprocess.check_output(
            ["ninja", "-C", build_root, "-t", "targets", "rule", rule_name]
        )
        .decode()
        .strip()
        .splitlines()
    )


def ninja_build(build_root: str, targets: list[str]):
    if targets:
        subprocess.check_call(["ninja", "-C", build_root, "--", *targets])


def main():
    import argparse

    ap = argparse.ArgumentParser(description="Build required targets for clang-tidy")
    ap.add_argument("build_root", help="Ninja build root", type=str)

    args = ap.parse_args()

    custom_commands = get_targets_of_rule(args.build_root, "CUSTOM_COMMAND")

    targets = (
        # Generated headers from embedded files
        [t for t in custom_commands if t.endswith(".gen.hh")]
        # Generated include files
        + [t for t in custom_commands if t.endswith(".gen.inc")]
        # Flex/Bison generated parsers
        + [t for t in custom_commands if t.endswith("-tab.cc")]
        # Kaitai Struct generated parsers
        + [t for t in custom_commands if t.endswith(".cpp") and "kaitai" in t]
        # Perl XS generated bindings
        + [t for t in custom_commands if t.endswith(".cc") and "perl" in t.lower()]
    )
    ninja_build(args.build_root, targets)


if __name__ == "__main__":
    main()
