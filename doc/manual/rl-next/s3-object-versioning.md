---
synopsis: "S3 URLs now support object versioning via versionId parameter"
prs: [14274]
issues: [13955]
---

S3 URLs now support a `versionId` query parameter to fetch specific versions
of objects from S3 buckets with versioning enabled. This allows pinning to
exact object versions for reproducibility and protection against unexpected
changes:

```
s3://bucket/key?region=us-east-1&versionId=abc123def456
```
