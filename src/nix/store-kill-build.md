R""(

# Examples

* Terminate the active build of a derivation:

  ```console
  # nix store kill-build /nix/store/abc123-package.drv
  ```

# Description

This command asks the Nix process that owns the relevant output locks for the specified [store path] or registered [store derivation] to terminate.
It sends `SIGTERM` and waits up to five seconds for the process to release the locks.
If any lock remains held, the command fails without sending `SIGKILL`, which could leave builder processes running after the locks are released.

Nix cannot determine the output locks of an unregistered store derivation, such as one sent directly to a remote builder with the `BuildDerivation` protocol operation.
Specify a known output path instead.
An unregistered [floating content-addressing derivation] has no known output path before it finishes, so its build cannot currently be selected.

Output locks are also used during substitution and registration.
If either operation owns a selected lock, this command terminates its Nix process.

The command signals only the verified owner of each relevant output lock.
It does not signal processes that only have the lock file open or are waiting to acquire the lock.

The command does not remove lock files.
Instead, it waits for the operating system to release the locks, allowing Nix's normal locking protocol to continue safely.

A Nix process can manage multiple builds, so terminating it can cancel other builds.

The selected store must support this operation.
The Nix daemon permits only trusted clients to use it.

On Linux, the kernel must support pidfds.

[floating content-addressing derivation]: @docroot@/store/derivation/outputs/content-address.md#floating
[store derivation]: @docroot@/glossary.md#gloss-store-derivation
[store path]: @docroot@/store/store-path.md

)""
