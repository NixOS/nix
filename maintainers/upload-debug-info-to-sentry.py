#!/usr/bin/env nix
#!nix shell --inputs-from . nixpkgs#sentry-cli nixpkgs#python3 nixpkgs#binutils --command python3

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

NAR_DIR = "/tmp/nars"
DEBUG_INFO_DIR = "/tmp/debug-info"


def get_dynamic_libraries(executable: str) -> list[str]:
    if platform.system() == "Darwin":
        result = subprocess.run(["otool", "-L", executable], capture_output=True, text=True, check=True)
        libs = []
        for line in result.stdout.splitlines()[1:]:  # skip first line (the binary path itself)
            # otool -L output lines look like:
            #   /nix/store/.../libfoo.dylib (compatibility version X.Y.Z, current version A.B.C)
            m = re.match(r"\s+(\S+)\s+\(", line)
            if m:
                libs.append(m.group(1))
        return libs
    else:
        result = subprocess.run(["ldd", executable], capture_output=True, text=True, check=True)
        libs = []
        for line in result.stdout.splitlines():
            # ldd output lines look like:
            #   libfoo.so.1 => /nix/store/.../libfoo.so.1 (0x...)
            #   /lib64/ld-linux-x86-64.so.2 (0x...)
            m = re.search(r"=> (/\S+)", line)
            if m:
                libs.append(m.group(1))
            elif line.strip().startswith("/"):
                path = line.strip().split()[0]
                libs.append(path)
        return libs


def get_build_id(path: str) -> str | None:
    result = subprocess.run(["readelf", "-n", path], capture_output=True, text=True)
    m = re.search(r"Build ID:\s+([0-9a-f]+)", result.stdout)
    return m.group(1) if m else None


def download_nar(build_id: str, archive: str) -> str:
    """Download a NAR to /tmp/nars and return the local path. Skips if already present."""
    base_url = f"https://cache.nixos.org/debuginfo/{build_id}"
    nar_url = urllib.parse.urljoin(base_url, archive)
    filename = nar_url.split("/")[-1]
    local_path = os.path.join(NAR_DIR, filename)
    if not os.path.exists(local_path):
        os.makedirs(NAR_DIR, exist_ok=True)
        print(f"    downloading {nar_url} ...", file=sys.stderr)
        urllib.request.urlretrieve(nar_url, local_path)
    else:
        print(f"    already have {filename}", file=sys.stderr)
    return local_path


def extract_debug_symbols(nar_path: str, member: str, build_id: str) -> str:
    """Extract a member from a .nar.xz into /tmp/debug-info/<build_id>.debug. Returns the output path."""
    out_path = os.path.join(DEBUG_INFO_DIR, f"{build_id}.debug")
    if os.path.exists(out_path):
        print(f"    already extracted {out_path}", file=sys.stderr)
        return out_path
    os.makedirs(DEBUG_INFO_DIR, exist_ok=True)
    print(f"    extracting {member} -> {out_path} ...", file=sys.stderr)
    xz = subprocess.Popen(["xz", "-d"], stdin=open(nar_path, "rb"), stdout=subprocess.PIPE)
    nar_cat = subprocess.run(
        ["nix", "nar", "cat", "/dev/stdin", member],
        stdin=xz.stdout,
        capture_output=True,
        check=True,
    )
    xz.wait()
    with open(out_path, "wb") as f:
        f.write(nar_cat.stdout)
    return out_path


def find_debug_file_in_dirs(build_id: str, debug_dirs: list[str]) -> str | None:
    """Look for a .debug file by build ID under <dir>/lib/debug/.build-id/NN/NNN.debug."""
    subpath = os.path.join("lib", "debug", ".build-id", build_id[:2], build_id[2:] + ".debug")
    for d in debug_dirs:
        candidate = os.path.join(d, subpath)
        if os.path.exists(candidate):
            return candidate
    return None


def fetch_debuginfo(build_id: str) -> dict | None:
    url = f"https://cache.nixos.org/debuginfo/{build_id}"
    try:
        with urllib.request.urlopen(url) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def main():
    parser = argparse.ArgumentParser(
        description="Upload debug symbols to Sentry."
    )
    parser.add_argument("executable", help="Path to the executable (e.g. ./result/bin/nix)")
    parser.add_argument("--project", help="Sentry project ID")
    parser.add_argument("--debug-dir", action="append", default=[], metavar="DIR",
                        help="Directory to search for debug files (may be repeated, Linux only)")
    args = parser.parse_args()

    libs = [args.executable] + get_dynamic_libraries(args.executable)

    if platform.system() == "Darwin":
        # On macOS there are no separate debug info files; upload the binaries directly.
        print("Files to upload:", file=sys.stderr)
        for lib in libs:
            print(f"  {lib}", file=sys.stderr)
        files_to_upload = libs
    else:
        debug_files = []
        print("ELF files to process:", file=sys.stderr)
        for lib in libs:
            debug_files.append(lib)

            build_id = get_build_id(lib)
            if build_id is None:
                print(f"  {lib} (no build ID, uploading binary)", file=sys.stderr)
                debug_files.append(lib)
                continue

            local = find_debug_file_in_dirs(build_id, args.debug_dir)
            if local:
                print(f"  {lib} ({build_id}): found locally at {local}", file=sys.stderr)
                debug_files.append(local)
                continue

            debuginfo = fetch_debuginfo(build_id)
            if debuginfo is None:
                print(f"  {lib} ({build_id}): no separate debug info", file=sys.stderr)
                continue

            print(f"  {lib} ({build_id}): member={debuginfo['member']}", file=sys.stderr)
            nar_path = download_nar(build_id, debuginfo["archive"])
            debug_file = extract_debug_symbols(nar_path, debuginfo["member"], build_id)
            debug_files.append(debug_file)
        files_to_upload = debug_files

    if files_to_upload:
        print(f"Uploading {len(files_to_upload)} file(s) to Sentry...", file=sys.stderr)
        cmd = ["sentry-cli", "debug-files", "upload"]
        if args.project:
            cmd += ["--project", args.project]
        subprocess.run(cmd + files_to_upload, check=True)


if __name__ == "__main__":
    main()
