---
synopsis: S3 substituters fall back to the URL's region for STS WebIdentity auth
prs: [15594]
---

When authenticating to an S3 binary cache via STS WebIdentity (EKS IRSA,
GitHub Actions OIDC), Nix now uses the `?region=` parameter from the S3 URL
as a fallback for the STS endpoint region if neither `AWS_REGION` nor
`AWS_DEFAULT_REGION` is set. Previously, IRSA setups that exported
`AWS_WEB_IDENTITY_TOKEN_FILE` and `AWS_ROLE_ARN` but no region would fail
with a misleading "IMDS provider" error.
