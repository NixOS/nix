---
synopsis: "libfetchers: fetch GitLab sources by the single commit endpoint"
prs: [16588]
---

`gitlab` sources in `libfetchers` are now fetched by their single commit endpoint, rather than the list endpoint, aligning it with the GitHub fetcher. This also seems to work around CloudFlare HTTP 403 errors on the list endpoint.
