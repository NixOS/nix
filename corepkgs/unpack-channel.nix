with import <nix/config.nix>;

let

  builder = builtins.toFile "unpack-channel.sh"
    ''
      mkdir $out
      cd $out
      ${bzip2} -d < $src | ${tar} xf - ${tarFlags}
      mv * $out/$channelName
      if [ -n "$binaryCacheURL" ]; then
        mkdir $out/binary-caches
        echo -n "$binaryCacheURL" > $out/binary-caches/$channelName
      fi
    '';

in

{ name, channelName, src, binaryCacheURL ? "" }:

derivation {
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" builder ];
  inherit name channelName src binaryCacheURL;

  PATH = "${nixBinDir}:${coreutils}";

  # No point in doing this remotely.
  preferLocalBuild = true;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
