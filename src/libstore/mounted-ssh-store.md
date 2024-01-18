R"(

**Store URL format**: `mounted-ssh-ng://[username@]hostname`

Experimental store type that allows full access to a Nix store on a remote machine,
and additionally requires that store be mounted in the local file system.

The mounting of that store is not managed by Nix, and must by managed manually.
It could be accomplished with SSHFS or NFS, for example.

The local file system is used to optimize certain operations.
For example, rather than serializing Nix archives and sending over the Nix channel,
we can directly access the file system data via the mount-point.

The local file system is also used to make certain operations possible that wouldn't otherwise be.
For example, persistent GC roots can be created if they reside on the same file system as the remote store:
the remote side will create the symlinks necessary to avoid race conditions.

> **Note**

> It is unfortunately that we need a separate scheme (and C++ class) for this, because of the current architecture of swappable store implementations.
> Ideally, the mounted/unmounted choice can just be an option on the existing `ssh-ng://` scheme.
> This new scheme will probably remain experimental as a temporary stop-gap until we can do the ideal thing we actually want.
)"
