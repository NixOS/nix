---
synopsis: Set FD_CLOEXEC on sockets created by curl
issues: []
prs: [12439]
---


Curl creates sockets without setting FD_CLOEXEC/SOCK_CLOEXEC, this can cause connections to remain open forever when using commands like `nix shell`

This change sets the FD_CLOEXEC flag using a CURLOPT_SOCKOPTFUNCTION callback.
