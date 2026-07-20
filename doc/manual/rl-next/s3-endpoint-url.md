---
synopsis: "S3: `endpoint=host:port` fixed, deprecated in favour of full URLs"
---

Bare `host:port` values for the `endpoint` parameter of `s3://` store URLs
were misparsed (the host was taken as a URL scheme). This is fixed, but the
bare form and the separate `scheme` parameter are deprecated: use a full URL
such as `endpoint=https://minio.example.com` instead.
