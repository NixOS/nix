---
synopsis: "S3 Transfer Acceleration support for faster remote cache access"
issues: [12973]
prs: [14277]
---

S3 binary cache stores now support AWS S3 Transfer Acceleration for faster
uploads and downloads when accessing buckets from geographically distant
locations.

Transfer Acceleration routes S3 requests through CloudFront edge locations,
which can significantly improve performance for users far from the bucket's
region. For example, US-based users accessing Tokyo-region buckets can see
substantial speed improvements.

To enable transfer acceleration, add `use-transfer-acceleration=true` to your
S3 URL:

```console
$ nix copy nixpkgs.hello \
  --to 's3://my-cache?region=ap-northeast-1&use-transfer-acceleration=true'
```

Requirements:
- Bucket names cannot contain dots (periods)
- Transfer Acceleration must be enabled on the S3 bucket
- Additional AWS charges apply for Transfer Acceleration

This feature only applies to AWS S3 buckets and has no effect when using custom
endpoints for S3-compatible services.
