---
synopsis: "S3 URLs now support skipping authentication for public buckets"
prs: [14463]
issues: [4857]
---

S3 URLs now support a `public=true` query parameter that instructs Nix to skip
all credential lookup attempts when accessing S3 buckets. This eliminates
timeout delays when working with publicly accessible S3 buckets and improves
reliability in environments where AWS credentials may be unavailable or
misconfigured.

**Example usage:**

```bash
# S3 binary cache store
nix copy --from 's3://nix-cache?public=true&region=us-east-1' /nix/store/...
```

```nix
# fetchurl with public S3 URL
builtins.fetchurl {
  url = "s3://public-bucket/file.tar.gz?public=true&region=us-east-1";
  sha256 = "...";
}
```

**Note:** The bucket must have appropriate public access policies configured on
the S3 side. Nix will not attempt to verify permissions - requests will fail
with HTTP 403 if the bucket is not publicly accessible.
