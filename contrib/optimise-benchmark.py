#!/usr/bin/env python3

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time


def run(command, environment, **kwargs):
    return subprocess.run(command, env=environment, check=True, **kwargs)


def allocation(paths):
    inodes = {}
    for path in paths:
        for file in path.iterdir():
            status = file.stat()
            inodes[(status.st_dev, status.st_ino)] = status
    return (
        sum(status.st_size for status in inodes.values()),
        sum(status.st_blocks * 512 for status in inodes.values()),
        len(inodes),
    )


def measure(command, environment, log):
    started = time.perf_counter()
    with log.open("wb") as output:
        process = subprocess.Popen(command, env=environment, stdout=output, stderr=output)
        _, status, usage = os.wait4(process.pid, 0)
        elapsed = time.perf_counter() - started
        process.returncode = os.waitstatus_to_exitcode(status)
    if process.returncode:
        raise RuntimeError(log.read_text())
    return {
        "wall_seconds": elapsed,
        "user_seconds": usage.ru_utime,
        "system_seconds": usage.ru_stime,
        "max_rss_kib": usage.ru_maxrss / (1024 if sys.platform == "darwin" else 1),
        "disk_read_bytes": usage.ru_inblock * 512,
        "disk_write_bytes": usage.ru_oublock * 512,
    }


def remove_store(directory):
    for parent, _, _ in os.walk(directory):
        os.chmod(parent, 0o700)
    shutil.rmtree(directory)


def binary_digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--baseline-revision")
    parser.add_argument("--directory", type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--paths", type=int, default=64)
    parser.add_argument("--files", type=int, default=16)
    parser.add_argument("--size", type=int, default=1024 * 1024)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--cache", choices=("warm", "cold"), default="warm")
    args = parser.parse_args()
    if min(args.paths, args.files, args.size, args.repetitions) < 1 or args.files % 4:
        parser.error("positive sizes required; --files must be a multiple of four")
    args.binary = args.binary.absolute()
    args.baseline = args.baseline.absolute()
    args.directory = args.directory.absolute()
    args.output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.pop("LD_LIBRARY_PATH", None)
    environment.pop("LD_PRELOAD", None)
    environment.update({
        "NIX_CONF_DIR": str(args.output.absolute() / "empty-conf"),
        "NIX_USER_CONF_FILES": "",
        "NIX_CONFIG": "auto-optimise-store = false\ngc-reserved-space = 0\nfsync-metadata = false",
        "NIX_REMOTE": "",
    })
    metadata = {
        "platform": platform.platform(),
        "cpu_count": os.cpu_count(),
        "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "arguments": {key: str(value) if isinstance(value, Path) else value
                      for key, value in vars(args).items()},
        "version": run([str(args.binary), "--version"], environment, capture_output=True, text=True).stdout.strip(),
        "baseline_version": run([str(args.baseline), "--version"], environment, capture_output=True, text=True).stdout.strip(),
        "binary_sha256": binary_digest(args.binary),
        "baseline_sha256": binary_digest(args.baseline),
        "resolved_binary": str(args.binary.resolve()),
        "resolved_baseline": str(args.baseline.resolve()),
    }
    for name in ("cpu.max", "memory.max"):
        control = Path("/sys/fs/cgroup") / name
        if control.exists():
            metadata[name] = control.read_text().strip()
    metadata["filesystem"] = subprocess.check_output(
        ["stat", "-f", "-c", "%T", str(args.directory)], text=True).strip() if sys.platform == "linux" else None
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    writer = None
    with (args.output / "results.csv").open("w", newline="") as output:
        for repetition in range(1, args.repetitions + 1):
            workers = [0, 1, 2, 4, 8, 16]
            offset = (repetition - 1) % len(workers)
            workers = workers[offset:] + workers[:offset]
            for jobs in workers:
                variant = "upstream" if jobs == 0 else "patched"
                binary = args.baseline if jobs == 0 else args.binary
                root = Path(tempfile.mkdtemp(prefix="nix-optimise-bench-", dir=args.directory))
                try:
                    store = root / "store"
                    store.mkdir()
                    uri = f"local?store={store}&state={root}/state&log={root}/log"
                    command = [str(binary), "--store", uri]
                    paths = []
                    for path_index in range(args.paths):
                        path = store / f"{path_index + 1:032d}-bench-{path_index}"
                        path.mkdir()
                        for file_index in range(args.files):
                            group = file_index // 2
                            key = f"shared-{group}" if file_index < args.files // 2 else f"{path_index}-{group}"
                            payload = hashlib.shake_256(key.encode()).digest(args.size)
                            (path / f"file-{file_index}").write_bytes(payload)
                        paths.append(path)
                    validity = "".join(f"{path}\n\n0\n" for path in paths)
                    run(command + ["--register-validity"], environment, input=validity, text=True, capture_output=True)
                    before, blocks_before, count_before = allocation(paths)
                    assert count_before == args.paths * args.files
                    assert all(file.stat().st_nlink == 1 for path in paths for file in path.iterdir())
                    if args.cache == "cold":
                        for path in paths:
                            for file in path.iterdir():
                                with file.open("rb") as source:
                                    os.fsync(source.fileno())
                                    os.posix_fadvise(source.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
                    optimise = command + ["--optimise"]
                    if jobs:
                        optimise += ["--option", "optimise-store-jobs", str(jobs)]
                    log = args.output / f"run-{repetition}-{jobs}.log"
                    metrics = measure(optimise, environment, log)
                    if "unknown setting" in log.read_text():
                        raise RuntimeError(log.read_text())
                    after, blocks_after, count_after = allocation(paths)
                    expected_count = args.files // 4 * (args.paths + 1)
                    assert count_after == expected_count, (count_after, expected_count)
                    run(command + ["--verify", "--check-contents"], environment, capture_output=True)
                    row = {
                        "repetition": repetition, "variant": variant, "workers": jobs, **metrics,
                        "bytes_saved": before - after,
                        "allocated_bytes_saved": blocks_before - blocks_after,
                    }
                    if writer is None:
                        writer = csv.DictWriter(output, fieldnames=list(row))
                        writer.writeheader()
                    writer.writerow(row)
                    output.flush()
                    print(json.dumps(row), flush=True)
                finally:
                    remove_store(root)


if __name__ == "__main__":
    main()
