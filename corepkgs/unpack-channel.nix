with import <nix/config.nix>;

{ name, src }:

derivation {
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" ./unpack-channel.sh ];
  inherit name src bzip2 tar tr;
  PATH = "${nixBinDir}:${coreutils}";
}
