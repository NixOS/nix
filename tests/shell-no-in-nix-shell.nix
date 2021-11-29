# This tries to replicate old nixpkgs behavior where `inNixShell` wasn't a supported attribute
args@{ ... }:
({ fooContents ? "noInNixShell" }: import ./shell.sh { inherit fooContents; }) args
