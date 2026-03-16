"""
Integration test for nix file-transfer retry backoff.

Runs an in-process flaky HTTP server (503 + Retry-After for N requests,
then 200) and asserts nix-prefetch-url retry timing/counts from stderr.
"""

import http.server
import re
import socketserver
import subprocess
import threading
import time

PAYLOAD = b"hello from flaky server\n"
PORT = 0  # set after server starts
fail_count = 0
retry_after = "1"
lock = threading.Lock()


class FlakyHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global fail_count
        with lock:
            n = fail_count
            if n > 0:
                fail_count = n - 1
        if n > 0:
            self.send_response(503)
            self.send_header("Retry-After", retry_after)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, *_args):
        pass


def set_failures(n: int, ra: int = 1) -> None:
    global fail_count, retry_after
    with lock:
        fail_count = n
        retry_after = str(ra)


def fetch(extra_opts: str = "") -> tuple[int, str, list[int]]:
    """Run nix-prefetch-url, return (exit_code, output, retry_delays_ms)."""
    cmd = f"nix-prefetch-url {extra_opts} http://127.0.0.1:{PORT}/file"
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    out = r.stdout + r.stderr
    delays = [int(m) for m in re.findall(r"retrying in (\d+) ms", out)]
    return r.returncode, out, delays


def main() -> None:
    global PORT
    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.TCPServer(("127.0.0.1", 0), FlakyHandler)
    PORT = httpd.server_address[1]
    print(f"Flaky server on port {PORT}")
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    try:
        test_rate_limit_delay_and_retry_after()
        test_retry_after_floor_exceeds_backoff()
        test_retry_exhaustion()
        test_raised_retry_attempts()
        test_download_attempts_alias()
    finally:
        httpd.shutdown()


def test_rate_limit_delay_and_retry_after():
    """503 uses rate-limit delay (5000ms base), Retry-After honored as floor."""
    set_failures(2, ra=2)
    start = time.time()
    rc, out, delays = fetch()
    elapsed = time.time() - start

    assert rc == 0, f"Expected success: {out}"
    assert len(delays) == 2, f"Expected 2 retries, got {len(delays)}: {out}"
    assert all(d >= 2000 for d in delays), f"Delays below Retry-After floor: {delays}"
    assert delays[0] >= 5000, f"First delay {delays[0]}ms < rate-limit base 5000ms"
    assert elapsed >= 4, f"Elapsed {elapsed:.1f}s too short"


def test_retry_after_floor_exceeds_backoff():
    """Retry-After=3s floors the delay even when computed backoff is lower."""
    set_failures(1, ra=3)
    rc, out, delays = fetch("--option filetransfer-retry-delay-rate-limited 500")

    assert rc == 0, f"Expected success: {out}"
    assert len(delays) == 1, f"Expected 1 retry: {out}"
    assert delays[0] >= 3000, f"Delay {delays[0]}ms < Retry-After 3000ms"


def test_retry_exhaustion():
    """Fails after filetransfer-retry-attempts exhausted."""
    set_failures(100, ra=0)
    rc, out, delays = fetch(
        "--option filetransfer-retry-attempts 3 "
        "--option filetransfer-retry-delay-rate-limited 100"
    )

    assert rc != 0, f"Expected failure: {out}"
    assert "HTTP error 503" in out, f"Missing 503 error: {out}"
    # attempts=3 means 1 initial + 2 retries
    assert len(delays) == 2, f"Expected 2 retries, got {len(delays)}: {out}"


def test_raised_retry_attempts():
    """More attempts allows eventual success."""
    set_failures(6, ra=0)
    rc, out, delays = fetch(
        "--option filetransfer-retry-attempts 8 "
        "--option filetransfer-retry-delay-rate-limited 50"
    )

    assert rc == 0, f"Expected success: {out}"
    assert len(delays) == 6, f"Expected 6 retries, got {len(delays)}: {out}"


def test_download_attempts_alias():
    """download-attempts backward-compat alias still works."""
    set_failures(100, ra=0)
    rc, out, delays = fetch(
        "--option download-attempts 2 --option filetransfer-retry-delay-rate-limited 50"
    )

    assert rc != 0, f"Expected failure: {out}"
    assert len(delays) == 1, f"Expected 1 retry, got {len(delays)}: {out}"


if __name__ == "__main__":
    main()
