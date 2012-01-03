with import <nix/config.nix>;

{ system, inputs }:

derivation {
  name = "channels";
  builder = shell;
  args = [ "-e" ./unpack-channel.sh ];
  inherit system inputs bzip2 tar tr;
  PATH = "${nixBinDir}:${coreutils}";
}
