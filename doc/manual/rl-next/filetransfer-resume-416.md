---
synopsis: "Complete downloads resumed at the end of the resource"
---

Nix can now complete a download if a connection fails after the whole response body has arrived.
When the retry receives HTTP 416, Nix checks that `Content-Range` reports a resource length equal to the number of bytes already received.
This fixes substitutions that previously failed with `HTTP error 416` after a binary cache had sent the whole file.
