# Security

To understand Nix's security model, it's important to differentiate between
[components that make up a typical Nix installation](@docroot@/architecture/architecture.md#overview).
The central part is the _Nix store_ and Nix components making up the _Store layer_ responsible for managing it.
Who can modify its contents dictates where trust boundaries lie.

The _store_ can be owned and managed exclusively by one user, or shared between multiple ones.
These two configurations are conventionally called “single-user” and “multi-user” modes.

> [!NOTE]
>
> Such a distinction is useful for describing various way to configure Nix, but doesn't
> unnecessarily reflect how Nix components function.

The [“single-user mode”](./single-user.md) is similar to what most other
package management tools do: there is a single user (typically root) who
performs all package management operations. All other users can then use the
installed packages, but they cannot perform package management operations
themselves.

In the [“multi-user mode”](./multi-user.md) case,
multiple users can perform package management operations — for instance, every
user can install software without requiring root privileges.
This is done by a privileged process (Nix daemon, typically run as root) on behalf of those
users who cannot modify files in the _store_ themselves.
In this mode, Nix also differentiates between _trusted_ and _untrusted_ users.
_untrusted_ users can perform a limited set of package-management operations.
_trusted_ users have effectively full control over the Nix daemon and the contents of the
_store_ and can achieve execution of arbitrary code in the context of that process via configurable hooks.

> [!NOTE]
>
> For this reason, [`trusted-users`](../command-ref/conf-file.md#conf-trusted-users) dissolve
> all trust boundaries and should be avoided if possible.
> In default multi-user installations where the daemon is run as `root`, using this feature is
> effectively equivalent to granting passwordless `sudo` to the user.

In [multi-user mode](./multi-user.md), Nix's primary concern is to ensure store integrity,
build purity and isolation across package management operations performed though Nix's API by _untrusted_ users.
For instance, it would be considered a high priority security bug for an unprivileged user to be able
to corrupt or overwrite a store object with arbitrary data.
Alternatively, a vulnerability in the Nix daemon allowing for arbitrary code execution by an _untrusted_ user
would also be considered a severe issue.

In most cases Nix doesn't manage inert data in the store, but software used
by e.g. NixOS where it contains core libraries used by the privileged Nix daemon
itself (e.g. `glibc`) and other privileged services. All officially supported installations
also place Nix libraries and executables into the _store_. Thus, a store corruption vulnerability
can directly lead to privilege escalation when the _store_ is referenced by privileged services.

<!-- TODO: Also link unprivileged daemon and highlight how even an unprivileged daemon
     doesn't solve the store object corruption vector. -->
