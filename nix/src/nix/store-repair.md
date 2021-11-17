R""(

# Examples

* Repair a store path, after determining that it is corrupt:

  ```console
  # nix store verify /nix/store/yb5q57zxv6hgqql42d5r8b5k5mcq6kay-hello-2.10
  path '/nix/store/yb5q57zxv6hgqql42d5r8b5k5mcq6kay-hello-2.10' was
  modified! expected hash
  'sha256:1hd5vnh6xjk388gdk841vflicy8qv7qzj2hb7xlyh8lpb43j921l', got
  'sha256:1a25lf78x5wi6pfkrxalf0n13kdaca0bqmjqnp7wfjza2qz5ssgl'

  # nix store repair /nix/store/yb5q57zxv6hgqql42d5r8b5k5mcq6kay-hello-2.10
  ```

# Description

This command attempts to "repair" the store paths specified by
*installables* by redownloading them using the available
substituters. If no substitutes are available, then repair is not
possible.

> **Warning**
>
> During repair, there is a very small time window during which the old
> path (if it exists) is moved out of the way and replaced with the new
> path. If repair is interrupted in between, then the system may be left
> in a broken state (e.g., if the path contains a critical system
> component like the GNU C Library).

)""
