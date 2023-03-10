# Experimental Python Bindings

Nix comes with minimal experimental Python bindings that link directly to the necessary dynamic libraries, making them very fast.

## Trying it out

The easiest way to try out the bindings is using the provided example environment:

```
$ nix run github:NixOS/nix#nix.python-bindings.exampleEnv
Python 3.10.8 (main, Oct 11 2022, 11:35:05) [GCC 11.3.0] on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> import nix
>>> nix.callExprString('"Hello ${name}!"', arg={"name": "Python"}))
'Hello Python!'
```

For the available functions and their interfaces, see the API section.

## Build integration

In the future these Python bindings will be available from Nixpkgs as `python3Packages.nix`.

Until then the Python bindings are only available from the Nix derivation via the `python-bindings` [passthru attribute](https://nixos.org/manual/nixpkgs/stable/#var-stdenv-passthru). Without any modifications, this derivation is built for the default Python 3 version from the Nixpkgs version used to build Nix. This Python version might not match the Python version of the project you're trying to use them in. Therefore it is recommended to override the bindings with the correct Python version using

```
nix.python-bindings.override {
  python = myPythonVersion;
}
```

For complete examples, see https://github.com/NixOS/nix/tree/master/python/examples
