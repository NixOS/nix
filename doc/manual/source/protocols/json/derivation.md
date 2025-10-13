# Derivation JSON Format

> **Warning**
>
> This JSON format is currently
> [**experimental**](@docroot@/development/experimental-features.md#xp-feature-nix-command)
> and subject to change.

The JSON serialization of a
[derivations](@docroot@/glossary.md#gloss-store-derivation)
is a JSON object with the following fields:

* `name`:
  The name of the derivation.
  This is used when calculating the store paths of the derivation's outputs.

* `version`:
  Must be `3`.
  This is a guard that allows us to continue evolving this format.
  The choice of `3` is fairly arbitrary, but corresponds to this informal version:

  - Version 0: A-Term format

  - Version 1: Original JSON format, with ugly `"r:sha256"` inherited from A-Term format.

  - Version 2: Separate `method` and `hashAlgo` fields in output specs

  - Version 3: Drop store dir from store paths, just include base name.

  Note that while this format is experimental, the maintenance of versions is best-effort, and not promised to identify every change.

* `outputs`:
  Information about the output paths of the derivation.
  This is a JSON object with one member per output, where the key is the output name and the value is a JSON object with these fields:

  * `path`:
    The output path, if it is known in advanced.
    Otherwise, `null`.


  * `method`:
    For an output which will be [content addressed], a string representing the [method](@docroot@/store/store-object/content-address.md) of content addressing that is chosen.
    Valid method strings are:

    - [`flat`](@docroot@/store/store-object/content-address.md#method-flat)
    - [`nar`](@docroot@/store/store-object/content-address.md#method-nix-archive)
    - [`text`](@docroot@/store/store-object/content-address.md#method-text)
    - [`git`](@docroot@/store/store-object/content-address.md#method-git)

    Otherwise, `null`.

  * `hashAlgo`:
    For an output which will be [content addressed], the name of the hash algorithm used.
    Valid algorithm strings are:

    - `blake3`
    - `md5`
    - `sha1`
    - `sha256`
    - `sha512`

  * `hash`:
    For fixed-output derivations, the expected content hash in base-16.

  > **Example**
  >
  > ```json
  > "outputs": {
  >   "out": {
  >     "method": "nar",
  >     "hashAlgo": "sha256",
  >     "hash": "6fc80dcc62179dbc12fc0b5881275898f93444833d21b89dfe5f7fbcbb1d0d62"
  >   }
  > }
  > ```

* `inputSrcs`:
  A list of store paths on which this derivation depends.

  > **Example**
  >
  > ```json
  > "inputSrcs": [
  >   "47y241wqdhac3jm5l7nv0x4975mb1975-separate-debug-info.sh",
  >   "56d0w71pjj9bdr363ym3wj1zkwyqq97j-fix-pop-var-context-error.patch"
  > ]
  > ```

* `inputDrvs`:
  A JSON object specifying the derivations on which this derivation depends, and what outputs of those derivations.

  > **Example**
  >
  > ```json
  > "inputDrvs": {
  >   "6lkh5yi7nlb7l6dr8fljlli5zfd9hq58-curl-7.73.0.drv": ["dev"],
  >   "fn3kgnfzl5dzym26j8g907gq3kbm8bfh-unzip-6.0.drv": ["out"]
  > }
  > ```

  specifies that this derivation depends on the `dev` output of `curl`, and the `out` output of `unzip`.

* `system`:
  The system type on which this derivation is to be built
  (e.g. `x86_64-linux`).

* `builder`:
  The absolute path of the program to be executed to run the build.
  Typically this is the `bash` shell
  (e.g. `/nix/store/r3j288vpmczbl500w6zz89gyfa4nr0b1-bash-4.4-p23/bin/bash`).

* `args`:
  The command-line arguments passed to the `builder`.

* `env`:
  The environment passed to the `builder`.

* `structuredAttrs`:
  [Structured Attributes](@docroot@/store/derivation/index.md#structured-attrs), only defined if the derivation contains them.
  Structured attributes are JSON, and thus embedded as-is.
