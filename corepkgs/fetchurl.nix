with import <nix/config.nix>;

{system ? builtins.currentSystem, url, outputHash ? "", outputHashAlgo ? "", md5 ? "", sha1 ? "", sha256 ? ""}:

assert (outputHash != "" && outputHashAlgo != "")
    || md5 != "" || sha1 != "" || sha256 != "";

let

  builder = builtins.toFile "fetchurl.sh"
    ''
      echo "downloading $url into $out"
      ${curl} --fail --location --max-redirs 20 --insecure "$url" > "$out"
    '';

in
    
derivation {
  name = baseNameOf (toString url);
  builder = shell;
  args = [ "-e" builder ];

  # New-style output content requirements.
  outputHashAlgo = if outputHashAlgo != "" then outputHashAlgo else
      if sha256 != "" then "sha256" else if sha1 != "" then "sha1" else "md5";
  outputHash = if outputHash != "" then outputHash else
      if sha256 != "" then sha256 else if sha1 != "" then sha1 else md5;
  
  inherit system url;

  # No need to double the amount of network traffic
  preferLocalBuild = true;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
