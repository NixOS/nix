---
synopsis: "S3: document that environment credentials override `profile`"
issues: 16811
---

Since 2.33.2, `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY` take
precedence over the `profile` parameter of an `s3://` store URL.
The [S3 Binary Cache Store](@docroot@/store/types/s3-binary-cache-store.md)
docs now say so.
