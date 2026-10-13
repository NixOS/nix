---
synopsis: AWS credentials are now cached across S3 requests
prs: [16209]
---

Nix now caches the AWS credentials it resolves for S3 binary caches instead of
re-running the full credential provider chain on every request. Previously,
credential sources that require a network round trip (IMDS instance profiles,
ECS container credentials, STS WebIdentity, SSO) were queried once per
substitution. Credentials with an expiration timestamp are refreshed
automatically 5 minutes before they expire; credentials without one (e.g.
static credentials from the environment or `~/.aws/credentials`) are re-read
every 15 minutes, matching the behavior of the AWS SDK's default credential
provider chain.
