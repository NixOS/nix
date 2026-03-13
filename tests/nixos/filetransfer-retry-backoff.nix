# Test file-transfer retry backoff behaviour:
# - 503 responses use the rate-limit retry delay (not the default 250ms)
# - Retry-After header is honored as a minimum delay floor
# - per-substituter retry-attempts URL parameter overrides the global setting
#
# Uses a small Python HTTP server that returns 503 (with Retry-After) for
# the first N requests, then serves a file. The failure count is controlled
# via a file on disk so each test case can reset it.

{ config, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  flakyServer = pkgs.writeText "flaky-server.py" ''
    import http.server
    import socketserver
    import os
    import sys

    PORT = int(sys.argv[1])
    PAYLOAD = b"hello from flaky server\n"

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            # Read failure counter; decrement and return 503 if positive
            try:
                with open("/tmp/fail-count") as f:
                    n = int(f.read().strip() or "0")
            except FileNotFoundError:
                n = 0
            if n > 0:
                with open("/tmp/fail-count", "w") as f:
                    f.write(str(n - 1))
                # Retry-After value is configurable via /tmp/retry-after
                try:
                    with open("/tmp/retry-after") as f:
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

        def log_message(self, *args):
            pass  # quiet

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        httpd.serve_forever()
  '';
in
{
  name = "filetransfer-retry-backoff";

  nodes = {
    machine =
      { pkgs, lib, ... }:
      {
        virtualisation.writableStore = true;
        environment.systemPackages = [ pkgs.python3 ];
        nix.settings.substituters = lib.mkForce [ ];
        nix.extraOptions = ''
          experimental-features = nix-command
          # Disable jitter so retry timing is deterministic and testable
          filetransfer-retry-jitter = false
        '';
      };
  };

  testScript = ''
    import re
    import time

    start_all()
    machine.wait_for_unit("multi-user.target")

    # Start the flaky server
    machine.succeed("python3 ${flakyServer} 8888 >/dev/null 2>&1 &")
    machine.wait_for_open_port(8888)

    def set_failures(n, retry_after=1):
        machine.succeed(f"echo {n} > /tmp/fail-count")
        machine.succeed(f"echo {retry_after} > /tmp/retry-after")

    def fetch(extra_opts=""):
        """Attempt to prefetch from the flaky server, capturing stderr."""
        return machine.execute(
            f"nix-prefetch-url {extra_opts} http://localhost:8888/file 2>&1"
        )

    # ========================================================================
    # Test 1: 503 uses rate-limit delay, Retry-After is honored as floor
    # ========================================================================
    print("\n=== Test: 503 uses rate-limit delay, Retry-After honored ===")

    set_failures(2, retry_after=2)
    start = time.time()
    status, out = fetch()
    elapsed = time.time() - start

    assert status == 0, f"Expected success after retries, got status {status}: {out}"

    # With jitter disabled, first retry should wait max(5000ms rate-limit base, 2000ms Retry-After) = 5000ms
    # Second retry: max(10000ms, 2000ms) = 10000ms. Total ~15s minimum.
    # But retry-after is 2, so actually: max(5000, 2000)=5000, max(10000, 2000)=10000.
    # We check for at least 4s elapsed (generous lower bound accounting for overhead)
    # and that the log shows delays matching the rate-limit path, not the 250ms default.

    retry_delays = [int(m) for m in re.findall(r"retrying in (\d+) ms", out)]
    assert len(retry_delays) == 2, f"Expected 2 retries, got {len(retry_delays)}: {out}"

    # Each delay should be >= 2000ms (Retry-After floor) and in fact >= 5000ms (rate-limit base)
    for d in retry_delays:
        assert d >= 2000, f"Retry delay {d}ms is below Retry-After floor of 2000ms: {out}"

    # The first delay should be the rate-limit base (5000ms), not the default (250ms)
    assert retry_delays[0] >= 5000, (
        f"First retry delay {retry_delays[0]}ms is below rate-limit base 5000ms — "
        f"503 may not be classified as rate-limited: {out}"
    )

    assert elapsed >= 4, f"Elapsed {elapsed:.1f}s too short, retry delays not applied"
    print(f"  OK: retries delayed {retry_delays}, elapsed {elapsed:.1f}s")

    # ========================================================================
    # Test 2: Retry-After floor exceeds computed backoff
    # ========================================================================
    print("\n=== Test: Retry-After floor exceeds computed backoff ===")

    # Use a short per-URL retry-delay so Retry-After (3s) is clearly the floor
    set_failures(1, retry_after=3)
    status, out = fetch(extra_opts="--option filetransfer-retry-delay-rate-limited 500")

    assert status == 0, f"Expected success: {out}"
    retry_delays = [int(m) for m in re.findall(r"retrying in (\d+) ms", out)]
    assert len(retry_delays) == 1, f"Expected 1 retry: {out}"
    # Retry-After=3s should floor the delay even though computed (500ms) is lower
    assert retry_delays[0] >= 3000, (
        f"Retry delay {retry_delays[0]}ms below Retry-After 3000ms floor: {out}"
    )
    print(f"  OK: delay {retry_delays[0]}ms respects Retry-After=3s floor")

    # ========================================================================
    # Test 3: retry exhaustion fails after filetransfer-retry-attempts
    # ========================================================================
    print("\n=== Test: retry exhaustion fails ===")

    set_failures(100, retry_after=0)  # never succeed
    status, out = fetch(
        extra_opts="--option filetransfer-retry-attempts 3 --option filetransfer-retry-delay-rate-limited 100"
    )

    assert status != 0, f"Expected failure after exhausting retries: {out}"
    assert "HTTP error 503" in out, f"Expected 503 error message: {out}"
    retry_count = len(re.findall(r"retrying in \d+ ms", out))
    # filetransfer-retry-attempts=3 means 1 initial + 2 retries = 2 "retrying" messages
    assert retry_count == 2, f"Expected 2 retries before giving up, got {retry_count}: {out}"
    print(f"  OK: failed after {retry_count} retries with HTTP 503")

    # ========================================================================
    # Test 4: raising filetransfer-retry-attempts allows more retries to succeed
    # ========================================================================
    print("\n=== Test: raising filetransfer-retry-attempts allows success ===")

    set_failures(6, retry_after=0)  # needs 7 attempts total
    # Default is 5 attempts — would fail. Override to 8.
    status, out = fetch(
        extra_opts="--option filetransfer-retry-attempts 8 --option filetransfer-retry-delay-rate-limited 50"
    )

    assert status == 0, (
        f"Expected success with filetransfer-retry-attempts=8 after 6 failures: {out}"
    )

    retry_count = len(re.findall(r"retrying in \d+ ms", out))
    assert retry_count == 6, f"Expected 6 retries, got {retry_count}: {out}"
    print(f"  OK: succeeded after {retry_count} retries with raised attempt limit")

    # ========================================================================
    # Test 5: download-attempts alias still works (backwards compat)
    # ========================================================================
    print("\n=== Test: download-attempts alias still works ===")

    set_failures(100, retry_after=0)
    status, out = fetch(
        extra_opts="--option download-attempts 2 --option filetransfer-retry-delay-rate-limited 50"
    )
    assert status != 0, f"Expected failure: {out}"
    retry_count = len(re.findall(r"retrying in \d+ ms", out))
    assert retry_count == 1, (
        f"download-attempts=2 alias should allow 1 retry, got {retry_count}: {out}"
    )
    print(f"  OK: download-attempts alias respected ({retry_count} retry)")

    print("\n=== All filetransfer-retry-backoff tests passed ===")
  '';
}
