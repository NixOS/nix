R"(

**Store URL format**: `s3://`*bucket-name*

This store allows reading and writing a binary cache stored in an AWS S3 (or S3-compatible service) bucket.
This store shares many idioms with the [HTTP Binary Cache Store](@docroot@/store/types/http-binary-cache-store.md).

For AWS S3, the binary cache URL for a bucket named `example-nix-cache` will be exactly <s3://example-nix-cache>.
For S3 compatible binary caches, consult that cache's documentation.

### Anonymous reads to your S3-compatible binary cache

> If your binary cache is publicly accessible and does not require authentication,
> it is simplest to use the [HTTP Binary Cache Store] rather than S3 Binary Cache Store with
> <https://example-nix-cache.s3.amazonaws.com> instead of <s3://example-nix-cache>.

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

### S3 Transfer Acceleration

For faster uploads and downloads when accessing S3 buckets from geographically distant locations, you can enable AWS S3 Transfer Acceleration. This routes transfers through CloudFront edge locations for improved performance.

To enable transfer acceleration, add the `use-transfer-acceleration=true` parameter to your S3 URL:

```console
$ nix copy nixpkgs.hello \
  --to 's3://example-nix-cache?use-transfer-acceleration=true&region=ap-northeast-1'
```

#### Requirements for Transfer Acceleration

- Bucket names cannot contain dots (`.`) - e.g., `my.bucket.name` will not work
- Transfer Acceleration must be enabled on your S3 bucket (see [AWS documentation](https://docs.aws.amazon.com/AmazonS3/latest/userguide/transfer-acceleration.html))
- Additional charges apply for Transfer Acceleration (see AWS pricing)

> **Note**
>
> Transfer Acceleration only works with AWS S3 buckets. It has no effect when using custom endpoints for S3-compatible services like MinIO.

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

- To use S3 Transfer Acceleration for faster transfers from distant locations:

  ```console
  $ nix copy nixpkgs.hello \
    --to 's3://my-cache?profile=cache-upload&region=ap-northeast-1&use-transfer-acceleration=true'
  ```

)"
