{ name, channelName, src }:

derivation {
  builder = "builtin:unpack-channel";

  system = "builtin";

  inherit name channelName src;

  # No point in doing this remotely.
  preferLocalBuild = true;
}
