with import <nix/config.nix>;

{system ? builtins.currentSystem, url, outputHash ? "", outputHashAlgo ? "", md5 ? "", sha1 ? "", sha256 ? "", executable ? false}:

assert (outputHash != "" && outputHashAlgo != "")
    || md5 != "" || sha1 != "" || sha256 != "";

let

  builder = builtins.toFile "fetchurl.sh"
    (''
      echo "downloading $url into $out"
      ${curl} --fail --location --max-redirs 20 --insecure "$url" > "$out"
    '' + (if executable then "${coreutils}/chmod +x $out" else ""));

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
  outputHashMode = if executable then "recursive" else "flat";
  
  inherit system url;

  # No need to double the amount of network traffic
  preferLocalBuild = true;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;

  impureEnvVars = [
    # We borrow these environment variables from the caller to allow
    # easy proxy configuration.  This is impure, but a fixed-output
    # derivation like fetchurl is allowed to do so since its result is
    # by definition pure.
    "http_proxy" "https_proxy" "ftp_proxy" "all_proxy" "no_proxy"
  ];
}
