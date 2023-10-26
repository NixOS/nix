let

  stdenvFun = { }: { name = "stdenv"; };
  stdenv2Fun = { }: { name = "stdenv2"; };
  fetchurlFun = { stdenv }: assert stdenv.name == "stdenv"; { name = "fetchurl"; };
  atermFun = { stdenv, fetchurl }: { name = "aterm-${stdenv.name}"; };
  aterm2Fun = { stdenv, fetchurl }: { name = "aterm2-${stdenv.name}"; };
  nixFun = { stdenv, fetchurl, aterm }: { name = "nix-${stdenv.name}-${aterm.name}"; };
  
  mplayerFun =
    { stdenv, fetchurl, enableX11 ? false, xorg ? null, enableFoo ? true, foo ? null  }:
    assert stdenv.name == "stdenv2";
    assert enableX11 -> xorg.libXv.name == "libXv";
    assert enableFoo -> foo != null;
    { name = "mplayer-${stdenv.name}.${xorg.libXv.name}-${xorg.libX11.name}"; };

  makeOverridable = f: origArgs: f origArgs //
    { override = newArgs:
        makeOverridable f (origArgs // (if builtins.isFunction newArgs then newArgs origArgs else newArgs));
    };
    
  callPackage_ = pkgs: f: args:
    makeOverridable f ((builtins.intersectAttrs (builtins.functionArgs f) pkgs) // args);

  allPackages =
    { overrides ? (pkgs: pkgsPrev: { }) }:
    let
      callPackage = callPackage_ pkgs;
      pkgs = pkgsStd // (overrides pkgs pkgsStd);
      pkgsStd = {
        inherit pkgs;
        stdenv = callPackage stdenvFun { };
        stdenv2 = callPackage stdenv2Fun { };
        fetchurl = callPackage fetchurlFun { };
        aterm = callPackage atermFun { };
        xorg = callPackage xorgFun { };
        mplayer = callPackage mplayerFun { stdenv = pkgs.stdenv2; enableFoo = false; };
        nix = callPackage nixFun { };
      };
    in pkgs;

  libX11Fun = { stdenv, fetchurl }: { name = "libX11"; };
  libX11_2Fun = { stdenv, fetchurl }: { name = "libX11_2"; };
  libXvFun = { stdenv, fetchurl, libX11 }: { name = "libXv"; };
  
  xorgFun =
    { pkgs }:
    let callPackage = callPackage_ (pkgs // pkgs.xorg); in
    {
      libX11 = callPackage libX11Fun { };
      libXv = callPackage libXvFun { };
    };

in

let

  pkgs = allPackages { };
  
  pkgs2 = allPackages {
    overrides = pkgs: pkgsPrev: {
      stdenv = pkgs.stdenv2;
      nix = pkgsPrev.nix.override { aterm = aterm2Fun { inherit (pkgs) stdenv fetchurl; }; };
      xorg = pkgsPrev.xorg // { libX11 = libX11_2Fun { inherit (pkgs) stdenv fetchurl; }; };
    };
  };
  
in

  [ pkgs.stdenv.name
    pkgs.fetchurl.name
    pkgs.aterm.name
    pkgs2.aterm.name
    pkgs.xorg.libX11.name
    pkgs.xorg.libXv.name
    pkgs.mplayer.name
    pkgs2.mplayer.name
    pkgs.nix.name
    pkgs2.nix.name
  ]
