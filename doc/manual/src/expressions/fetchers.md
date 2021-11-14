**Warning**:
This section is **experimental** and its interface is subject to change.

# Fetchers

Nix supports fetching source trees
from different locations
through the `fetchTree` builtin.

Below we document the supported backends
and their configuration options.

## Git

Allows you to fetch a Git source tree (similar to a `git clone`).

> **Note**
>
> The `.git/` folder won't be saved for reproducibility.

Options:

- `type`

  Must be equal to `"git"`.
- `url`

  Location of the git repository as accepted by `git`
  (i.e. a local path, http(s) or SSH URL).
- `ref`

  The git ref to look for the requested revision under.
  This is often a branch or tag name.
  Defaults to `HEAD`.

  If `ref` does not start with `refs/`, it will be prefixed with `refs/heads/`.
- `rev`

  The git revision to fetch. Defaults to the tip of `ref`.
- `submodules`

  A Boolean parameter that specifies
  whether submodules should be checked out.
  Defaults to `false`.
- `shallow`

  If `true` only the HEAD commit will be fetched.
  Defaults to `true`.
- `allRefs`

  Whether to fetch all refs of the repository.

  With this argument being true,
  it's possible to load a `rev` from *any* `ref`
  (by default only `rev`s from the specified `ref` are supported).
  Defaults to `false`.


Examples:

- To fetch a private repository over SSH:

  ```nix
  builtins.fetchTree {
    type = "git";
    url = "git@github.com:my-secret/repo.git";
    ref = "main";
    rev = "2aeb84a3aac9bba4f9b7aa8731d35f3d6925b40f";
  }
  ```

- To fetch an arbitrary reference:

  ```nix
  builtins.fetchTree {
    type = "git";
    url = "https://github.com/NixOS/nix.git";
    ref = "refs/heads/0.5-release";
  }
  ```

- If the revision you're looking for is in the default branch of
  the git repository you don't strictly need to specify the branch
  name in the `ref` attribute.

  However, if the revision you're looking for is in a future
  branch for the non-default branch you will need to specify the
  the `ref` attribute as well.

  ```nix
  builtins.fetchTree {
    type = "git";
    url = "https://github.com/nixos/nix.git";
    rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
    ref = "1.11-maintenance";
  }
  ```

  > **Note**
  >
  > It is nice to always specify the branch which a revision
  > belongs to. Without the branch being specified, the fetcher
  > might fail if the default branch changes. Additionally, it can
  > be confusing to try a commit from a non-default branch and see
  > the fetch fail. If the branch is specified the fault is much
  > more obvious.

- If the revision you're looking for is in the default branch of
  the git repository you may omit the `ref` attribute.

  ```nix
  builtins.fetchTree {
    type = "git";
    url = "https://github.com/nixos/nix.git";
    rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
  }
  ```

- To fetch a specific tag:

  ```nix
  builtins.fetchTree {
    type = "git";
    url = "https://github.com/nixos/nix.git";
    ref = "refs/tags/1.9";
  }
  ```

- To fetch the latest version of a remote branch:

  ```nix
  builtins.fetchTree {
    type = "git";
    url = "ssh://git@github.com/nixos/nix.git";
    ref = "master";
  }
  ```

  > **Note**
  >
  > Nix will refetch the branch in accordance with
  > the option `tarball-ttl`.

  > **Note**
  >
  > Fetching the latest version of a remote branch
  > is disabled in *Pure evaluation mode*.
