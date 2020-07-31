# Serving a Nix store via S3

Nix has built-in support for storing and fetching store paths from
Amazon S3 and S3-compatible services. This uses the same *binary*
cache mechanism that Nix usually uses to fetch prebuilt binaries from
[cache.nixos.org](https://cache.nixos.org/).

The following options can be specified as URL parameters to the S3 URL:

  - `profile`  
    The name of the AWS configuration profile to use. By default Nix
    will use the `default` profile.

  - `region`  
    The region of the S3 bucket. `us–east-1` by default.
    
    If your bucket is not in `us–east-1`, you should always explicitly
    specify the region parameter.

  - `endpoint`  
    The URL to your S3-compatible service, for when not using Amazon S3.
    Do not specify this value if you're using Amazon S3.
    
    > **Note**
    > 
    > This endpoint must support HTTPS and will use path-based
    > addressing instead of virtual host based addressing.

  - `scheme`  
    The scheme used for S3 requests, `https` (default) or `http`. This
    option allows you to disable HTTPS for binary caches which don't
    support it.
    
    > **Note**
    > 
    > HTTPS should be used if the cache might contain sensitive
    > information.

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
for authenticating requests to Amazon S3.

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
for authenticating requests to Amazon S3.

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
