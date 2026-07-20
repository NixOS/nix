---
synopsis: Google Cloud Storage binary cache (`gs://`)
prs: []
issues: []
---

Nix can now use a Google Cloud Storage bucket as a binary cache via the new `gs://bucket-name` store URI.
Reads and writes go through the GCS XML API (S3-compatible),
so multipart uploads and the existing `multipart-*` settings work unchanged.

Authentication uses Google [Application Default Credentials](https://cloud.google.com/docs/authentication/application-default-credentials)
(service-account keys, `gcloud auth application-default login`, or the GCE/GKE metadata server)

`builtins.fetchurl` and substituters also accept plain `gs://bucket/object` URLs.
