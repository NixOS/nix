"""
Lightweight integration test for nix file-transfer retry backoff.

Starts a flaky HTTP server (in-process, threaded) that returns 503 with
Retry-After for the first N requests, then 200.  Exercises nix-prefetch-url
and asserts on retry timing / counts from stderr.
"""

import http.server
import os
import re
import socketserver
import subprocess
import threading
import time

TMPDIR = os.environ.get("TMPDIR", "/tmp")
FAIL_COUNT_FILE = os.path.join(TMPDIR, "fail-count")
RETRY_AFTER_FILE = os.path.join(TMPDIR, "retry-after")
PAYLOAD = b"hello from flaky server\n"


class FlakyHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            with open(FAIL_COUNT_FILE) as f:
                n = int(f.read().strip() or "0")
        except FileNotFoundError:
            n = 0

        if n > 0:
            with open(FAIL_COUNT_FILE, "w") as f:
                f.write(str(n - 1))
            try:
                with open(RETRY_AFTER_FILE) as f:
                    ra = f.read().strip()
            except FileNotFoundError:
                ra = "1"
            self.send_response(503)
            self.send_header("Retry-After", ra)
            self.end_headers()
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, *_args):
        pass  # quiet


def set_failures(n: int, retry_after: int = 1) -> None:
    with open(FAIL_COUNT_FILE, "w") as f:
        f.write(str(n))
    with open(RETRY_AFTER_FILE, "w") as f:
        f.write(str(retry_after))


def fetch(port: int, extra_opts: str = "") -> tuple[int, str]:
    """Run nix-prefetch-url against the flaky server, return (status, combined output)."""
    cmd = f"nix-prefetch-url {extra_opts} http://127.0.0.1:{port}/file"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    # nix-prefetch-url prints retry messages and errors to stderr
    output = result.stdout + result.stderr
    return result.returncode, output


def main() -> None:
    # Bind to port 0 so the OS assigns a free ephemeral port — avoids
    # collisions when tests run in parallel or port 8888 is already taken.
    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.TCPServer(("127.0.0.1", 0), FlakyHandler)
    port = httpd.server_address[1]
    print(f"Flaky server listening on port {port}")

    server_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    server_thread.start()

    try:
        run_tests(port)
    finally:
        httpd.shutdown()

    print("\n=== All filetransfer-retry-backoff tests passed ===")


def run_tests(port: int) -> None:
    # ==================================================================
    # Test 1: 503 uses rate-limit delay, Retry-After is honored as floor
    # ==================================================================
    print("\n=== Test: 503 uses rate-limit delay, Retry-After honored ===")

    set_failures(2, retry_after=2)
    start = time.time()
    status, out = fetch(port)
    elapsed = time.time() - start

    assert status == 0, f"Expected success after retries, got status {status}: {out}"

    # With jitter disabled, first retry should wait
    #   max(5000ms rate-limit base, 2000ms Retry-After) = 5000ms
    # Second retry: max(10000ms, 2000ms) = 10000ms.
    retry_delays = [int(m) for m in re.findall(r"retrying in (\d+) ms", out)]
    assert len(retry_delays) == 2, f"Expected 2 retries, got {len(retry_delays)}: {out}"

    for d in retry_delays:
        assert d >= 2000, (
            f"Retry delay {d}ms is below Retry-After floor of 2000ms: {out}"
        )

    # First delay must be rate-limit base (5000ms), not the default (250ms)
    assert retry_delays[0] >= 5000, (
        f"First retry delay {retry_delays[0]}ms is below rate-limit base 5000ms — "
        f"503 may not be classified as rate-limited: {out}"
    )

    assert elapsed >= 4, f"Elapsed {elapsed:.1f}s too short, retry delays not applied"
    print(f"  OK: retries delayed {retry_delays}, elapsed {elapsed:.1f}s")

    # ==================================================================
    # Test 2: Retry-After floor exceeds computed backoff
    # ==================================================================
    print("\n=== Test: Retry-After floor exceeds computed backoff ===")

    set_failures(1, retry_after=3)
    status, out = fetch(
        port, extra_opts="--option filetransfer-retry-delay-rate-limited 500"
    )

    assert status == 0, f"Expected success: {out}"
    retry_delays = [int(m) for m in re.findall(r"retrying in (\d+) ms", out)]
    assert len(retry_delays) == 1, f"Expected 1 retry: {out}"
    assert retry_delays[0] >= 3000, (
        f"Retry delay {retry_delays[0]}ms below Retry-After 3000ms floor: {out}"
    )
    print(f"  OK: delay {retry_delays[0]}ms respects Retry-After=3s floor")

    # ==================================================================
    # Test 3: retry exhaustion fails after filetransfer-retry-attempts
    # ==================================================================
    print("\n=== Test: retry exhaustion fails ===")

    set_failures(100, retry_after=0)
    status, out = fetch(
        port,
        extra_opts=(
            "--option filetransfer-retry-attempts 3 "
            "--option filetransfer-retry-delay-rate-limited 100"
        ),
    )

    assert status != 0, f"Expected failure after exhausting retries: {out}"
    assert "HTTP error 503" in out, f"Expected 503 error message: {out}"
    retry_count = len(re.findall(r"retrying in \d+ ms", out))
    # filetransfer-retry-attempts=3 means 1 initial + 2 retries
    assert retry_count == 2, (
        f"Expected 2 retries before giving up, got {retry_count}: {out}"
    )
    print(f"  OK: failed after {retry_count} retries with HTTP 503")

    # ==================================================================
    # Test 4: raising filetransfer-retry-attempts allows more retries
    # ==================================================================
    print("\n=== Test: raising filetransfer-retry-attempts allows success ===")

    set_failures(6, retry_after=0)
    status, out = fetch(
        port,
        extra_opts=(
            "--option filetransfer-retry-attempts 8 "
            "--option filetransfer-retry-delay-rate-limited 50"
        ),
    )

    assert status == 0, (
        f"Expected success with filetransfer-retry-attempts=8 after 6 failures: {out}"
    )

    retry_count = len(re.findall(r"retrying in \d+ ms", out))
    assert retry_count == 6, f"Expected 6 retries, got {retry_count}: {out}"
    print(f"  OK: succeeded after {retry_count} retries with raised attempt limit")

    # ==================================================================
    # Test 5: download-attempts alias still works (backwards compat)
    # ==================================================================
    print("\n=== Test: download-attempts alias still works ===")

    set_failures(100, retry_after=0)
    status, out = fetch(
        port,
        extra_opts=(
            "--option download-attempts 2 "
            "--option filetransfer-retry-delay-rate-limited 50"
        ),
    )
    assert status != 0, f"Expected failure: {out}"
    retry_count = len(re.findall(r"retrying in \d+ ms", out))
    assert retry_count == 1, (
        f"download-attempts=2 alias should allow 1 retry, got {retry_count}: {out}"
    )
    print(f"  OK: download-attempts alias respected ({retry_count} retry)")


if __name__ == "__main__":
    main()
