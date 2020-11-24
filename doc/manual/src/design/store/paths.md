# Store Paths

A store path is a pair of a 20-byte digest and a name.

Historically it is the triple of those two and also the store directory, but the modern implementation's internal representation is just the pair.
This change is because in the vast majority of cases, the store dir is fully determined by the context in which the store path occurs.

## String representation

A store path is rendered as the concatenation of

  - the store directory

  - a path-separator (`/`)

  - the digest rendered as Base-32 (20 bytes becomes 32 bytes)

  - a hyphen (`-`)

  - the name

Let's take the store path from the very beginning of this manual as an example:

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1/

This parses like so:

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1/
    ^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^
    store dir  digest                           name
