---
synopsis: "S3 binary cache stores now support storage class configuration"
prs: [14464]
issues: [7015]
---

S3 binary cache stores now support configuring the storage class for uploaded objects via the `storage-class` parameter. This allows users to optimize costs by selecting appropriate storage tiers based on access patterns.

Example usage:

```bash
# Use Glacier storage for long-term archival
nix copy --to 's3://my-bucket?storage-class=GLACIER' /nix/store/...

# Use Intelligent Tiering for automatic cost optimization
nix copy --to 's3://my-bucket?storage-class=INTELLIGENT_TIERING' /nix/store/...
```

The storage class applies to both regular uploads and multipart uploads. When not specified, objects use the bucket's default storage class.

See the [S3 storage classes documentation](https://docs.aws.amazon.com/AmazonS3/latest/userguide/storage-class-intro.html) for available storage classes and their characteristics.
