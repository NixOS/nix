---
synopsis: "`unix://` stores can read store objects over the socket"
prs: [16748]
---

The [`unix://`](@docroot@/store/types/local-daemon-store.md) store has a new setting, `local-fs`.
It defaults to `true`: the client reads store objects from its own file system, and only writes and metadata queries go to the daemon.
With `local-fs=false`, the client reads store objects over the socket, as `ssh-ng://` does.
Use this for a daemon that serves a store the client cannot see, such as one in a virtual machine or a container.
