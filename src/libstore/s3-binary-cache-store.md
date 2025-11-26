R"(

**Store URL format**: `s3://`*bucket-name*

This store allows reading and writing a binary cache stored in an AWS S3 (or S3-compatible service) bucket.
This store shares many idioms with the [HTTP Binary Cache Store](@docroot@/store/types/http-binary-cache-store.md).

For AWS S3, the binary cache URL for a bucket named `example-nix-cache` will be exactly <s3://example-nix-cache>.
For S3 compatible binary caches, consult that cache's documentation.

### Anonymous reads to your S3-compatible binary cache

For publicly accessible binary caches that don't require authentication, you have two options:

**Option 1: HTTP URLs (Recommended)**

Use direct HTTPS URLs to avoid S3 credential lookup:

```
# AWS S3 (virtual-hosted style)
https://bucket-name.s3.region.amazonaws.com

# AWS S3 (path style)
https://s3.region.amazonaws.com/bucket-name

# S3-compatible services
https://endpoint/bucket-name
```

This approach bypasses AWS credential lookup entirely, avoiding timeouts on non-AWS infrastructure.

**Option 2: S3 URLs**

You can still use `s3://bucket-name` URLs, though this may be slower due to credential lookup attempts before falling back to unauthenticated access.

Your bucket will need a
[bucket policy](https://docs.aws.amazon.com/AmazonS3/v1/userguide/bucket-policies.html)
like the following to be accessible:

```json
{
    "Id": "DirectReads",
    "Version": "2012-10-17",
    "Statement": [
        {
            "Sid": "AllowDirectReads",
            "Action": [
                "s3:GetObject",
                "s3:GetBucketLocation",
                "s3:ListBucket"
            ],
            "Effect": "Allow",
            "Resource": [
                "arn:aws:s3:::example-nix-cache",
                "arn:aws:s3:::example-nix-cache/*"
            ],
            "Principal": "*"
        }
    ]
}
```

### Authentication

Nix will use the
[default credential provider chain](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/credentials.html)
for authenticating requests to Amazon S3.

Note that this means Nix will read environment variables and files with different idioms than with Nix's own settings, as implemented by the AWS SDK.
Consult the documentation linked above for further details.

### Authenticated reads to your S3 binary cache

Your bucket will need a bucket policy allowing the desired users to perform the `s3:GetObject`, `s3:GetBucketLocation`, and `s3:ListBucket` actions on all objects in the bucket.
The [anonymous policy given above](#anonymous-reads-to-your-s3-compatible-binary-cache) can be updated to have a restricted `Principal` to support this.

### Authenticated writes to your S3-compatible binary cache

Your account will need an IAM policy to support uploading to the bucket:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "UploadToCache",
      "Effect": "Allow",
      "Action": [
        "s3:AbortMultipartUpload",
        "s3:GetBucketLocation",
        "s3:GetObject",
        "s3:ListBucket",
        "s3:ListBucketMultipartUploads",
        "s3:ListMultipartUploadParts",
        "s3:PutObject"
      ],
      "Resource": [
        "arn:aws:s3:::example-nix-cache",
        "arn:aws:s3:::example-nix-cache/*"
      ]
    }
  ]
}
```

### Examples

With bucket policies and authentication set up as described above, uploading works via [`nix copy`](@docroot@/command-ref/new-cli/nix3-copy.md) (experimental).

- To upload with a specific credential profile for Amazon S3:

  ```console
  $ nix copy nixpkgs.hello \
    --to 's3://example-nix-cache?profile=cache-upload&region=eu-west-2'
  ```

- To upload to an S3-compatible binary cache:

  ```console
  $ nix copy nixpkgs.hello --to \
    's3://example-nix-cache?profile=cache-upload&scheme=https&endpoint=minio.example.com'
  ```

)"
