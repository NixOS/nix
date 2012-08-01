with import <nix/config.nix>;

let

  builder = builtins.toFile "unpack-channel.sh"
    ''
      mkdir $out
      cd $out
      ${bzip2} -d < $src | ${tar} xf - --warning=no-timestamp 
      mv * $out/$channelName
    '';

in

{ name, channelName, src }:

derivation {
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" builder ];
  inherit name channelName src;

  PATH = "${nixBinDir}:${coreutils}";

  # No point in doing this remotely.
  preferLocalBuild = true;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
