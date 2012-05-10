with import <nix/config.nix>;

{ name, channelName, src }:

derivation {
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" ./unpack-channel.sh ];
  inherit name channelName src bzip2 tar tr;
  PATH = "${nixBinDir}:${coreutils}";
  
  # No point in doing this remotely.
  preferLocalBuild = true;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
