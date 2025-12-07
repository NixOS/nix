#!/usr/bin/env python3
"""
Strips PCH (precompiled header) arguments from a compilation database.

This is needed because clang-tidy cannot process PCH flags when using
nixpkgs' cc-wrapper. The cc-wrapper makes assumptions that don't hold
when the compiler is being used indirectly by clang-tidy.

See: https://github.com/mesonbuild/meson/issues/13499
"""

import json
import shlex


def process_compdb(compdb: list[dict]) -> list[dict]:
    def munch_command(args: list[str]) -> list[str]:
        out = []
        eat_next = False
        for i, arg in enumerate(args):
            if arg in ["-fpch-preprocess", "-fpch-instantiate-templates"]:
                # -fpch-preprocess as used with gcc
                # -fpch-instantiate-templates as used by clang
                continue
            elif arg == "-include-pch" or (
                arg == "-include"
                and i + 1 < len(args)
                and args[i + 1] == "precompiled-headers.hh"
            ):
                # -include-pch some-pch (clang), or -include some-pch (gcc)
                eat_next = True
                continue
            if not eat_next:
                out.append(arg)
            eat_next = False
        return out

    def chomp(item: dict) -> dict:
        item = item.copy()
        item["command"] = shlex.join(munch_command(shlex.split(item["command"])))
        return item

    def cmdfilter(item: dict) -> bool:
        file = item["file"]
        # Filter out precompiled header files
        if file.endswith("precompiled-headers.hh"):
            return False
        # Filter out Rust files
        if file.endswith(".rs"):
            return False
        # Filter out Kaitai Struct generated code (uses reserved identifiers)
        if "kaitai" in file:
            return False
        # Filter out Flex/Bison generated parsers (generated code)
        if file.endswith("-tab.cc"):
            return False
        # Filter out Perl XS generated bindings (generated code)
        if "/perl/" in file and file.endswith(".cc"):
            return False
        return True

    return [chomp(x) for x in compdb if cmdfilter(x)]


def main():
    import argparse

    ap = argparse.ArgumentParser(
        description="Strip PCH arguments from compilation database"
    )
    ap.add_argument("input", type=argparse.FileType("r"), help="Input json file")
    ap.add_argument("output", type=argparse.FileType("w"), help="Output json file")
    args = ap.parse_args()

    input_json = json.load(args.input)
    json.dump(process_compdb(input_json), args.output, indent=2)


if __name__ == "__main__":
    main()
