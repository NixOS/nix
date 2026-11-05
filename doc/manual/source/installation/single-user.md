# Single-User Mode

In single-user mode, all Nix operations that access the database in `prefix/var/nix/db` or modify the Nix store in `prefix/store`
must be performed under the user ID that owns those directories.
This is typically root.
(If you install from RPM packages, that’s in fact the default ownership.)
Note that in rootful installations, it's crucial to use _build users_ (as described in [Multi-User Mode](./multi-user.md)).
It's also possible to use Nix without build users, but note that it [nullifies the effectiveness](https://github.com/NixOS/nix/issues/16645) of the build sandbox,
and allows [derivation builders](@docroot@/language/derivations.md#attr-builder) to modify their input closure.
Running builds as `root` with an empty `build-users-group` should be avoided, because it prevents Nix
dropping root capabilities before executing the build.
