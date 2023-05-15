R""(

# Description

`nix profile` allows you to create and manage *Nix profiles*. A Nix
profile is a set of packages that can be installed and upgraded
independently from each other. Nix profiles are versioned, allowing
them to be rolled back easily.

# Files

)""

#include "doc/files/profiles.md.gen.hh"

R""(

### Profile compatibility

> **Warning**
>
> Once you have used [`nix profile`] you can no longer use [`nix-env`] without first deleting `$XDG_STATE_HOME/nix/profiles/profile`

[`nix-env`]: @docroot@/command-ref/nix-env.md
[`nix profile`]: @docroot@/command-ref/new-cli/nix3-profile.md

Once you installed a package with [`nix profile`], you get the following error message when using [`nix-env`]:

```console
$ nix-env -f '<nixpkgs>' -iA 'hello'
error: nix-env
profile '/home/alice/.local/state/nix/profiles/profile' is incompatible with 'nix-env'; please use 'nix profile' instead
```

To migrate back to `nix-env` you can delete your current profile:

> **Warning**
>
> This will delete packages that have been installed before, so you may want to back up this information before running the command.

```console
 $ rm -rf "${XDG_STATE_HOME-$HOME/.local/state}/nix/profiles/profile"
```

)""
