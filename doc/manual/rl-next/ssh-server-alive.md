---
synopsis: SSH remote stores now send keep-alive messages by default
prs: [15620]
---

Nix now passes `-o ServerAliveInterval=30 -o ServerAliveCountMax=3` to `ssh`
when connecting to `ssh://` and `ssh-ng://` stores (including remote builders
used by the build hook). Previously, if a remote builder rebooted or became
unreachable mid-build without closing the TCP connection, the local `ssh`
process — and with it the build — would block indefinitely on a half-open
connection.

The interval and count are configurable per store via the new
`ssh-server-alive-interval` and `ssh-server-alive-count-max` store settings;
set the interval to `0` to disable and defer to `ssh_config`. Options passed
via `NIX_SSHOPTS` continue to take precedence.
