---
synopsis: "Increase retry delays on Http 429"
issues:
prs: [13052]
---

When downloading Nix, the retry delay was previously set to 0.25 seconds. It has now been increased to 1 minute to better handle transient CI errors, particularly on GitHub.

