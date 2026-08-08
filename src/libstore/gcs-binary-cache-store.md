R"(

**Store URL format**: `gs://`*bucket-name*

This store allows reading and writing a [binary cache](@docroot@/protocols/binary-cache/index.md) stored in a Google Cloud Storage bucket.
It speaks the GCS [XML API], which is wire-compatible with S3, so it shares its multipart upload implementation and limits with the [S3 Binary Cache Store](@docroot@/store/types/s3-binary-cache-store.md).

[XML API]: https://cloud.google.com/storage/docs/xml-api/overview

### Authentication

Nix authenticates to GCS using [Application Default Credentials][adc].
The following sources are tried in order:

1. The file pointed to by the `GOOGLE_APPLICATION_CREDENTIALS` environment variable (a service-account key or `gcloud` user credentials).
2. `$CLOUDSDK_CONFIG/application_default_credentials.json`, otherwise `~/.config/gcloud/application_default_credentials.json` (created by `gcloud auth application-default login`).
3. The GCE/GKE metadata server, when running on Google Cloud.

If no credentials are found, requests are sent unauthenticated, which works for public buckets.
Workload-identity federation (`external_account` credentials) is not yet supported.

[adc]: https://cloud.google.com/docs/authentication/application-default-credentials

### Permissions

For reading, the principal needs the `storage.objects.get` permission on the bucket (e.g. the `roles/storage.objectViewer` role).

For writing, the principal additionally needs `storage.objects.create` and `storage.objects.delete` (e.g. the `roles/storage.objectAdmin` role); the latter is required so multipart uploads can be aborted on failure.

### Examples

- To upload to a GCS bucket using the ambient ADC credentials:

  ```console
  $ nix copy nixpkgs.hello --to gs://example-nix-cache
  ```

- To use a requester-pays bucket, billing the access to your project:

  ```console
  $ nix copy nixpkgs.hello \
    --to 'gs://example-nix-cache?user-project=my-billing-project'
  ```

- To target a GCS-compatible emulator:

  ```console
  $ nix copy nixpkgs.hello \
    --to 'gs://example-nix-cache?endpoint=http://localhost:4443'
  ```

)"
