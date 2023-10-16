# Serving a Nix store via S3

Nix has [built-in support](@docroot@/command-ref/new-cli/nix3-help-stores.md#s3-binary-cache-store)
for storing and fetching store paths from
Amazon S3 and S3-compatible services. This uses the same *binary*
cache mechanism that Nix usually uses to fetch prebuilt binaries from
[cache.nixos.org](https://cache.nixos.org/).

In this example we will use the bucket named `example-nix-cache`.

## Anonymous Reads to your S3-compatible binary cache

If your binary cache is publicly accessible and does not require
authentication, the simplest and easiest way to use Nix with your S3
compatible binary cache is to use the HTTP URL for that cache.

For AWS S3 the binary cache URL for example bucket will be exactly
<https://example-nix-cache.s3.amazonaws.com> or
<s3://example-nix-cache>. For S3 compatible binary caches, consult that
cache's documentation.

Your bucket will need the following bucket policy:

```json
{
    "Id": "DirectReads",
    "Version": "2012-10-17",
    "Statement": [
        {
            "Sid": "AllowDirectReads",
            "Action": [
                "s3:GetObject",
                "s3:GetBucketLocation"
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

## Authenticated Reads to your S3 binary cache

For AWS S3 the binary cache URL for example bucket will be exactly
<s3://example-nix-cache>.

Nix will use the [default credential provider
chain](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/credentials.html)
for authenticating requests to Amazon S3. *On macOS:* the multi-user nix
installer configures `nix-daemon` such that the [location of shared `config`
and `credentials`
files](https://docs.aws.amazon.com/sdkref/latest/guide/file-location.html) are
`/etc/nix/aws/config` and `/etc/nix/aws/credentials`, respectively.

Nix supports authenticated reads from Amazon S3 and S3 compatible binary
caches.

Your bucket will need a bucket policy allowing the desired users to
perform the `s3:GetObject` and `s3:GetBucketLocation` action on all
objects in the bucket. The [anonymous policy given
above](#anonymous-reads-to-your-s3-compatible-binary-cache) can be
updated to have a restricted `Principal` to support this.

## Authenticated Writes to your S3-compatible binary cache

Nix support fully supports writing to Amazon S3 and S3 compatible
buckets. The binary cache URL for our example bucket will be
<s3://example-nix-cache>.

Nix will use the [default credential provider
chain](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/credentials.html)
for authenticating requests to Amazon S3. *On macOS:* the multi-user nix
installer configures `nix-daemon` such that the [location of shared `config`
and `credentials`
files](https://docs.aws.amazon.com/sdkref/latest/guide/file-location.html) are
`/etc/nix/aws/config` and `/etc/nix/aws/credentials`, respectively.

Your account will need the following IAM policy to upload to the cache:

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

## Examples

To upload with a specific credential profile for Amazon S3:

```console
$ nix copy nixpkgs.hello \
  --to 's3://example-nix-cache?profile=cache-upload&region=eu-west-2'
```

To upload to an S3-compatible binary cache:

```console
$ nix copy nixpkgs.hello --to \
  's3://example-nix-cache?profile=cache-upload&scheme=https&endpoint=minio.example.com'
```
