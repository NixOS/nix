---
synopsis: "Daemon option forwarding now uses a negotiated allow list"
---

Clients connecting through the local Nix daemon now avoid forwarding
client-only settings, such as `use-xdg-base-directories`, `store`, and
`print-missing`.

New clients and daemons negotiate a worker-protocol feature through which the
daemon reports which additional `SetOptions` overrides it accepts from that
connection. This takes the connection's trust level into account. The daemon
continues to validate every received setting and every requested substituter.

When the feature is unavailable, daemon-applicable overrides continue to use
the previous `SetOptions` behavior and the receiving daemon enforces its legacy
trust rules. A new client still omits options classified as client-only.
