{
  probe,
  globalTmpDir,
  outsideDir,
}:

builtins.mapAttrs
  (
    name: attributes:
    derivation (
      {
        name = "darwin-network-sandbox-${name}";
        system = builtins.currentSystem;
        builder = builtins.storePath probe;
        args = [ (if attributes.__darwinAllowLocalNetworking or false then "local" else "default") ];
        inherit globalTmpDir outsideDir;
      }
      // attributes
    )
  )
  {
    default = { };
    disabled.__darwinAllowLocalNetworking = false;
    enabled.__darwinAllowLocalNetworking = true;
  }
