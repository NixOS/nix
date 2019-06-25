with import <nix/config.nix>;

let

  builder = builtins.toFile "unpack-channel.sh"
    ''
      mkdir $out
      cd $out
      xzpat="\.xz\$"
      gzpat="\.gz\$"
      if [[ "$src" =~ $xzpat ]]; then
        ${xz} -d < $src | ${tar} xf - ${tarFlags}
      elif [[ "$src" =~ $gzpat ]]; then
        ${gzip} -d < $src | ${tar} xf - ${tarFlags}
      else
        ${bzip2} -d < $src | ${tar} xf - ${tarFlags}
      fi
      if [ * != $channelName ]; then
        mv * $out/$channelName
      fi
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

  inherit chrootDeps;
}
