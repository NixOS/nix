# Arguments and Variables

    ...
    
    rec { 
    
      hello = import ../applications/misc/hello/ex-1  { 
        inherit fetchurl stdenv perl;
      };
    
      perl = import ../development/interpreters/perl { 
        inherit fetchurl stdenv;
      };
    
      fetchurl = import ../build-support/fetchurl {
        inherit stdenv; ...
      };
    
      stdenv = ...;
    
    }

The Nix expression in [???](#ex-hello-nix) is a function; it is missing
some arguments that have to be filled in somewhere. In the Nix Packages
collection this is done in the file `pkgs/top-level/all-packages.nix`,
where all Nix expressions for packages are imported and called with the
appropriate arguments. [example\_title](#ex-hello-composition) shows
some fragments of `all-packages.nix`.

  - This file defines a set of attributes, all of which are concrete
    derivations (i.e., not functions). In fact, we define a *mutually
    recursive* set of attributes. That is, the attributes can refer to
    each other. This is precisely what we want since we want to “plug”
    the various packages into each other.

  - Here we *import* the Nix expression for GNU Hello. The import
    operation just loads and returns the specified Nix expression. In
    fact, we could just have put the contents of [???](#ex-hello-nix) in
    `all-packages.nix` at this point. That would be completely
    equivalent, but it would make the file rather bulky.
    
    Note that we refer to `../applications/misc/hello/ex-1`, not
    `../applications/misc/hello/ex-1/default.nix`. When you try to
    import a directory, Nix automatically appends `/default.nix` to the
    file name.

  - This is where the actual composition takes place. Here we *call* the
    function imported from `../applications/misc/hello/ex-1` with a set
    containing the things that the function expects, namely `fetchurl`,
    `stdenv`, and `perl`. We use inherit again to use the attributes
    defined in the surrounding scope (we could also have written
    `fetchurl = fetchurl;`, etc.).
    
    The result of this function call is an actual derivation that can be
    built by Nix (since when we fill in the arguments of the function,
    what we get is its body, which is the call to `stdenv.mkDerivation`
    in [???](#ex-hello-nix)).
    
    > **Note**
    > 
    > Nixpkgs has a convenience function `callPackage` that imports and
    > calls a function, filling in any missing arguments by passing the
    > corresponding attribute from the Nixpkgs set, like this:
    > 
    >     hello = callPackage ../applications/misc/hello/ex-1 { };
    > 
    > If necessary, you can set or override arguments:
    > 
    >     hello = callPackage ../applications/misc/hello/ex-1 { stdenv = myStdenv; };

  - Likewise, we have to instantiate Perl, `fetchurl`, and the standard
    environment.
