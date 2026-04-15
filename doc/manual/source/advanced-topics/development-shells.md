# Development Shells

`nix develop` can be used to enter a development shell for a derivation. Any derivation providing a `.devShell` attribute or having the `__isDevShell = true` attribute can be used as the target for `nix develop`.

You do not need `nix develop` to enter a derivation's development shell however. You can alternatively `nix run` a derivation's `.devShell` attribute, or manually run `bin/setup` from its default output.

The development shell's `meta.mainProgram` attribute should always be set to "setup".

## Structure

A development shell is just a derivation which provides the following files in the default output:

### `bin/setup`

This should be an executable file which when run provides the full interactive development shell. This can be a shell script with a `#!/path/to/posix/shell --rcfile` shebang line, or any executable.

It should be capable of consuming the following environment variables:

#### `NIX_DEVSHELL_VERBOSE`

If this variable is set, output additional debugging information. For example, a POSIX shell may respond to this by setting `set -x`.

#### `NIX_DEVSHELL_PHASE`

The name of the `stdenv` phase to execute, e.g. "build". See the `--phase` option of `nix develop`.

#### `NIX_DEVSHELL_COMMAND`

A command to execute in place of running an interactive shell, including space-separated arguments. Each component of the command (base command and arguments) will be single-quoted. For shell scripts, this should be passed as a quoted argument to `eval`.

#### `NIX_DEVSHELL_PROMPT` / `NIX_DEVSHELL_PROMPT_PREFIX` / `NIX_DEVSHELL_PROMPT_SUFFIX`

Used for customizing the interactive shell's prompt. A POSIX shell should use these to set the value of the `PS1` variable.

### `env`

`nix print-dev-env` will output this file verbatim. Its intention is to be a sourceable shell script that provides all of the shell variables and function definitions of a derivation's build environment.

Nix does not internally use this file, so it is okay to customize it for custom use cases.

### `env.json`

`nix print-dev-env --json` will output this file verbatim. It should have the following structure:

```json
{
    "bashFunctions": {
        "<fnName>": "<implementation>"
    },
    "variables": {
        "<name>": {
            "type": "<variable type>",
            "value": "<variable value>"
        }
    },
    "structuredAttrs": {
        ".attrs.sh": "<contents>",
        ".attrs.json": "<contents>"
    }
}
```

A variable's `type` can be one of:
- `exported`: corresponds to `declare -x`
- `var`: corresponds to `declare --`
- `array`: `corresponds to `declare -a`
- `associative`: `corresponds to `declare -A`
- `unknown` (no value attribute needed)

For example:

```json
{
    "bashFunctions": {
        "_addRpathPrefix": " \n    if [ \"${NIX_NO_SELF_RPATH:-0}\" != 1 ]; then\n        export NIX_LDFLAGS=\"-rpath $1/lib ${NIX_LDFLAGS-}\";\n    fi\n"
    },
    "variables": {
        "LD": {
            "type": "exported",
            "value": "ld"
        },
        "propagatedTargetDepFiles": {
            "type": "array",
            "value": [
                "propagated-target-target-deps"
            ]
        },
    }
}
```

Nix does not internally use this structure, so it is okay to customize it for custom use cases.
