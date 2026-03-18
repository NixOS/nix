---
synopsis: "S3: restore STS WebIdentity and ECS container credential providers"
prs: [15507]
---

Nix 2.33 replaced the S3 backend's `aws-sdk-cpp` credential chain with a
custom chain built on `aws-c-auth`. That chain omitted two providers,
breaking S3 binary cache access in container workloads:

- **STS WebIdentity** (`AWS_WEB_IDENTITY_TOKEN_FILE`, `AWS_ROLE_ARN`,
  `AWS_ROLE_SESSION_NAME`) — used by EKS IRSA, GitHub Actions OIDC, and
  any `sts:AssumeRoleWithWebIdentity` federation.
- **ECS container metadata** (`AWS_CONTAINER_CREDENTIALS_RELATIVE_URI`,
  `AWS_CONTAINER_CREDENTIALS_FULL_URI`) — used by ECS tasks and EKS Pod
  Identity.

The typical symptom was a misleading IMDS error
(`Valid credentials could not be sourced by the IMDS provider`), because
IMDS is the last provider tried after the correct one was skipped.

Both providers are now part of the chain, ordered to match the
pre-2.33 `DefaultAWSCredentialsProviderChain`:
`Environment → SSO → Profile → STS WebIdentity → (ECS | IMDS)`.
As in both the old and new AWS SDK default chains, ECS and IMDS are
mutually exclusive: when container credential environment variables are
set, IMDS is skipped.
