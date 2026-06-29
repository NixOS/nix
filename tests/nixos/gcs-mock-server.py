#!/usr/bin/env python3
"""Minimal GCS XML API mock for the gcs-binary-cache-store NixOS test.

This mock implements only, what that store needs and doubles as the OAuth2 token endpoint so the
authorized_user ADC flow can be tested end-to-end without faking signatures.
"""

import argparse
import json
import sys
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

OBJECTS: dict[str, dict[str, bytes]] = {}
UPLOADS: dict[str, dict] = {}
LOCK = threading.Lock()


def make_handler(args):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _parse(self):
            u = urlsplit(self.path)
            parts = u.path.lstrip("/").split("/", 1)
            bucket = parts[0]
            key = parts[1] if len(parts) > 1 else ""
            return bucket, key, parse_qs(u.query, keep_blank_values=True)

        def _authed(self, bucket: str) -> bool:
            if bucket in args.public_buckets:
                return True
            got = self.headers.get("Authorization", "")
            return got == f"Bearer {args.token}"

        def _send(self, code: int, body: bytes = b"", headers: dict | None = None):
            self.send_response(code)
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _body(self) -> bytes:
            n = int(self.headers.get("Content-Length", "0"))
            return self.rfile.read(n) if n else b""

        def log_message(self, fmt, *a):
            # Include the user-project header so the test can assert it was forwarded.
            up = self.headers.get("x-goog-user-project", "-")
            print(f"[gcs-mock] {fmt % a} user-project={up}", file=sys.stderr, flush=True)

        # OAuth2 token endpoint (authorized_user refresh flow)
        def _maybe_token_endpoint(self) -> bool:
            if urlsplit(self.path).path != "/token":
                return False
            self._body()  # drain
            self._send(
                200,
                json.dumps(
                    {
                        "access_token": args.token,
                        "expires_in": 3600,
                        "token_type": "Bearer",
                    }
                ).encode(),
                {"Content-Type": "application/json"},
            )
            return True

        # GCS XML API
        def do_HEAD(self):
            bucket, key, _ = self._parse()
            if not self._authed(bucket):
                return self._send(401)
            with LOCK:
                obj = OBJECTS.get(bucket, {}).get(key)
            self._send(200 if obj is not None else 404)

        def do_GET(self):
            bucket, key, _ = self._parse()
            if not self._authed(bucket):
                return self._send(401)
            with LOCK:
                obj = OBJECTS.get(bucket, {}).get(key)
            if obj is None:
                return self._send(404)
            self._send(200, obj, {"Content-Type": "application/octet-stream"})

        def do_PUT(self):
            bucket, key, q = self._parse()
            if not self._authed(bucket):
                return self._send(401)
            body = self._body()
            if "partNumber" in q and "uploadId" in q:
                uid = q["uploadId"][0]
                pn = int(q["partNumber"][0])
                with LOCK:
                    UPLOADS[uid]["parts"][pn] = body
                return self._send(200, headers={"ETag": f'"etag-{pn}"'})
            with LOCK:
                OBJECTS.setdefault(bucket, {})[key] = body
            self._send(200)

        def do_POST(self):
            if self._maybe_token_endpoint():
                return
            bucket, key, q = self._parse()
            if not self._authed(bucket):
                return self._send(401)
            self._body()  # drain
            if "uploads" in q:
                uid = uuid.uuid4().hex
                with LOCK:
                    UPLOADS[uid] = {"bucket": bucket, "key": key, "parts": {}}
                xml = (
                    "<InitiateMultipartUploadResult>"
                    f"<Bucket>{bucket}</Bucket><Key>{key}</Key>"
                    f"<UploadId>{uid}</UploadId>"
                    "</InitiateMultipartUploadResult>"
                )
                return self._send(
                    200, xml.encode(), {"Content-Type": "application/xml"}
                )
            if "uploadId" in q:
                uid = q["uploadId"][0]
                with LOCK:
                    up = UPLOADS.pop(uid)
                    blob = b"".join(up["parts"][i] for i in sorted(up["parts"]))
                    OBJECTS.setdefault(up["bucket"], {})[up["key"]] = blob
                return self._send(
                    200,
                    b"<CompleteMultipartUploadResult/>",
                    {"Content-Type": "application/xml"},
                )
            self._send(400)

        def do_DELETE(self):
            bucket, key, q = self._parse()
            if not self._authed(bucket):
                return self._send(401)
            if "uploadId" in q:
                with LOCK:
                    UPLOADS.pop(q["uploadId"][0], None)
                return self._send(204)
            with LOCK:
                OBJECTS.get(bucket, {}).pop(key, None)
            self._send(204)

    return Handler


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=4443)
    p.add_argument("--token", default="test-access-token")
    p.add_argument(
        "--public-bucket", dest="public_buckets", action="append", default=[]
    )
    args = p.parse_args()
    ThreadingHTTPServer(("0.0.0.0", args.port), make_handler(args)).serve_forever()


if __name__ == "__main__":
    main()
