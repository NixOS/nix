# Built-in Functions

This section lists the functions and constants built into the Nix
expression evaluator. (The built-in function `derivation` is discussed
above.) Some built-ins, such as `derivation`, are always in scope of
every Nix expression; you can just access them right away. But to
prevent polluting the namespace too much, most built-ins are not in
scope. Instead, you can access them through the `builtins` built-in
value, which is a set that contains all built-in functions and values.
For instance, `derivation` is also available as `builtins.derivation`.

  - `abort` *s*; `builtins.abort` *s*  
    Abort Nix expression evaluation, print error message *s*.

  - `builtins.add` *e1* *e2*  
    Return the sum of the numbers *e1* and *e2*.

  - `builtins.all` *pred* *list*  
    Return `true` if the function *pred* returns `true` for all elements
    of *list*, and `false` otherwise.

  - `builtins.any` *pred* *list*  
    Return `true` if the function *pred* returns `true` for at least one
    element of *list*, and `false` otherwise.

  - `builtins.attrNames` *set*  
    Return the names of the attributes in the set *set* in an
    alphabetically sorted list. For instance, `builtins.attrNames { y
    = 1; x = "foo"; }` evaluates to `[ "x" "y" ]`.

  - `builtins.attrValues` *set*  
    Return the values of the attributes in the set *set* in the order
    corresponding to the sorted attribute names.

  - `baseNameOf` *s*  
    Return the *base name* of the string *s*, that is, everything
    following the final slash in the string. This is similar to the GNU
    `basename` command.

  - `builtins.bitAnd` *e1* *e2*  
    Return the bitwise AND of the integers *e1* and *e2*.

  - `builtins.bitOr` *e1* *e2*  
    Return the bitwise OR of the integers *e1* and *e2*.

  - `builtins.bitXor` *e1* *e2*  
    Return the bitwise XOR of the integers *e1* and *e2*.

  - `builtins`  
    The set `builtins` contains all the built-in functions and values.
    You can use `builtins` to test for the availability of features in
    the Nix installation, e.g.,
    
        if builtins ? getEnv then builtins.getEnv "PATH" else ""
    
    This allows a Nix expression to fall back gracefully on older Nix
    installations that don’t have the desired built-in function.

  - `builtins.compareVersions` *s1* *s2*  
    Compare two strings representing versions and return `-1` if version
    *s1* is older than version *s2*, `0` if they are the same, and `1`
    if *s1* is newer than *s2*. The version comparison algorithm is the
    same as the one used by [`nix-env
                    -u`](#ssec-version-comparisons).

  - `builtins.concatLists` *lists*  
    Concatenate a list of lists into a single list.

  - `builtins.concatStringsSep` *separator* *list*  
    Concatenate a list of strings with a separator between each element,
    e.g. `concatStringsSep "/"
                    ["usr" "local" "bin"] == "usr/local/bin"`

  - `builtins.currentSystem`  
    The built-in value `currentSystem` evaluates to the Nix platform
    identifier for the Nix installation on which the expression is being
    evaluated, such as `"i686-linux"` or `"x86_64-darwin"`.

  - `builtins.deepSeq` *e1* *e2*  
    This is like `seq
                    e1
                    e2`, except that *e1* is evaluated *deeply*: if it’s a list or set,
    its elements or attributes are also evaluated recursively.

  - `derivation` *attrs*; `builtins.derivation` *attrs*  
    `derivation` is described in [???](#ssec-derivation).

  - `dirOf` *s*; `builtins.dirOf` *s*  
    Return the directory part of the string *s*, that is, everything
    before the final slash in the string. This is similar to the GNU
    `dirname` command.

  - `builtins.div` *e1* *e2*  
    Return the quotient of the numbers *e1* and *e2*.

  - `builtins.elem` *x* *xs*  
    Return `true` if a value equal to *x* occurs in the list *xs*, and
    `false` otherwise.

  - `builtins.elemAt` *xs* *n*  
    Return element *n* from the list *xs*. Elements are counted starting
    from 0. A fatal error occurs if the index is out of bounds.

  - `builtins.fetchurl` *url*  
    Download the specified URL and return the path of the downloaded
    file. This function is not available if [restricted evaluation
    mode](#conf-restrict-eval) is enabled.

  - `fetchTarball` *url*; `builtins.fetchTarball` *url*  
    Download the specified URL, unpack it and return the path of the
    unpacked tree. The file must be a tape archive (`.tar`) compressed
    with `gzip`, `bzip2` or `xz`. The top-level path component of the
    files in the tarball is removed, so it is best if the tarball
    contains a single directory at top level. The typical use of the
    function is to obtain external Nix expression dependencies, such as
    a particular version of Nixpkgs, e.g.
    
        with import (fetchTarball https://github.com/NixOS/nixpkgs/archive/nixos-14.12.tar.gz) {};
        
        stdenv.mkDerivation { … }
    
    The fetched tarball is cached for a certain amount of time (1 hour
    by default) in `~/.cache/nix/tarballs/`. You can change the cache
    timeout either on the command line with `--option tarball-ttl number
    of seconds` or in the Nix configuration file with this option: ` 
    number of seconds to cache `.
    
    Note that when obtaining the hash with ` nix-prefetch-url
                     ` the option `--unpack` is required.
    
    This function can also verify the contents against a hash. In that
    case, the function takes a set instead of a URL. The set requires
    the attribute `url` and the attribute `sha256`, e.g.
    
        with import (fetchTarball {
          url = "https://github.com/NixOS/nixpkgs/archive/nixos-14.12.tar.gz";
          sha256 = "1jppksrfvbk5ypiqdz4cddxdl8z6zyzdb2srq8fcffr327ld5jj2";
        }) {};
        
        stdenv.mkDerivation { … }
    
    This function is not available if [restricted evaluation
    mode](#conf-restrict-eval) is enabled.

  - `builtins.fetchGit` *args*  
    Fetch a path from git. *args* can be a URL, in which case the HEAD
    of the repo at that URL is fetched. Otherwise, it can be an
    attribute with the following attributes (all except `url` optional):
    
      - url  
        The URL of the repo.
    
      - name  
        The name of the directory the repo should be exported to in the
        store. Defaults to the basename of the URL.
    
      - rev  
        The git revision to fetch. Defaults to the tip of `ref`.
    
      - ref  
        The git ref to look for the requested revision under. This is
        often a branch or tag name. Defaults to `HEAD`.
        
        By default, the `ref` value is prefixed with `refs/heads/`. As
        of Nix 2.3.0 Nix will not prefix `refs/heads/` if `ref` starts
        with `refs/`.
    
      - submodules  
        A Boolean parameter that specifies whether submodules should be
        checked out. Defaults to `false`.
    
    Here are some examples of how to use `fetchGit`.
    
      - To fetch a private repository over SSH:
        
            builtins.fetchGit {
              url = "git@github.com:my-secret/repository.git";
              ref = "master";
              rev = "adab8b916a45068c044658c4158d81878f9ed1c3";
            }
    
      - To fetch an arbitrary reference:
        
            builtins.fetchGit {
              url = "https://github.com/NixOS/nix.git";
              ref = "refs/heads/0.5-release";
            }
    
      - If the revision you're looking for is in the default branch of
        the git repository you don't strictly need to specify the branch
        name in the `ref` attribute.
        
        However, if the revision you're looking for is in a future
        branch for the non-default branch you will need to specify the
        the `ref` attribute as well.
        
            builtins.fetchGit {
              url = "https://github.com/nixos/nix.git";
              rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
              ref = "1.11-maintenance";
            }
        
        > **Note**
        > 
        > It is nice to always specify the branch which a revision
        > belongs to. Without the branch being specified, the fetcher
        > might fail if the default branch changes. Additionally, it can
        > be confusing to try a commit from a non-default branch and see
        > the fetch fail. If the branch is specified the fault is much
        > more obvious.
    
      - If the revision you're looking for is in the default branch of
        the git repository you may omit the `ref` attribute.
        
            builtins.fetchGit {
              url = "https://github.com/nixos/nix.git";
              rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
            }
    
      - To fetch a specific tag:
        
            builtins.fetchGit {
              url = "https://github.com/nixos/nix.git";
              ref = "refs/tags/1.9";
            }
    
      - To fetch the latest version of a remote branch:
        
            builtins.fetchGit {
              url = "ssh://git@github.com/nixos/nix.git";
              ref = "master";
            }
        
        > **Note**
        > 
        > Nix will refetch the branch in accordance to
        > [???](#conf-tarball-ttl).
        
        > **Note**
        > 
        > This behavior is disabled in *Pure evaluation mode*.

  - `builtins.filter` *f* *xs*  
    Return a list consisting of the elements of *xs* for which the
    function *f* returns `true`.

  - `builtins.filterSource` *e1* *e2*  
    This function allows you to copy sources into the Nix store while
    filtering certain files. For instance, suppose that you want to use
    the directory `source-dir` as an input to a Nix expression, e.g.
    
        stdenv.mkDerivation {
          ...
          src = ./source-dir;
        }
    
    However, if `source-dir` is a Subversion working copy, then all
    those annoying `.svn` subdirectories will also be copied to the
    store. Worse, the contents of those directories may change a lot,
    causing lots of spurious rebuilds. With `filterSource` you can
    filter out the `.svn` directories:
    
    ``` 
      src = builtins.filterSource
        (path: type: type != "directory" || baseNameOf path != ".svn")
        ./source-dir;
    ```
    
    Thus, the first argument *e1* must be a predicate function that is
    called for each regular file, directory or symlink in the source
    tree *e2*. If the function returns `true`, the file is copied to the
    Nix store, otherwise it is omitted. The function is called with two
    arguments. The first is the full path of the file. The second is a
    string that identifies the type of the file, which is either
    `"regular"`, `"directory"`, `"symlink"` or `"unknown"` (for other
    kinds of files such as device nodes or fifos — but note that those
    cannot be copied to the Nix store, so if the predicate returns
    `true` for them, the copy will fail). If you exclude a directory,
    the entire corresponding subtree of *e2* will be excluded.

  - `builtins.foldl’` *op* *nul* *list*  
    Reduce a list by applying a binary operator, from left to right,
    e.g. `foldl’ op nul [x0 x1 x2 ...] = op (op
                    (op nul x0) x1) x2) ...`. The operator is applied strictly, i.e.,
    its arguments are evaluated first. For example, `foldl’ (x: y: x +
    y) 0 [1 2 3]` evaluates to 6.

  - `builtins.functionArgs` *f*  
    Return a set containing the names of the formal arguments expected
    by the function *f*. The value of each attribute is a Boolean
    denoting whether the corresponding argument has a default value. For
    instance, `functionArgs ({ x, y ? 123}: ...) = { x = false; y =
    true; }`.
    
    "Formal argument" here refers to the attributes pattern-matched by
    the function. Plain lambdas are not included, e.g. `functionArgs (x:
    ...) = { }`.

  - `builtins.fromJSON` *e*  
    Convert a JSON string to a Nix value. For example,
    
        builtins.fromJSON ''{"x": [1, 2, 3], "y": null}''
    
    returns the value `{ x = [ 1 2 3 ]; y = null;
                    }`.

  - `builtins.genList` *generator* *length*  
    Generate list of size *length*, with each element *i* equal to the
    value returned by *generator* `i`. For example,
    
        builtins.genList (x: x * x) 5
    
    returns the list `[ 0 1 4 9 16 ]`.

  - `builtins.getAttr` *s* *set*  
    `getAttr` returns the attribute named *s* from *set*. Evaluation
    aborts if the attribute doesn’t exist. This is a dynamic version of
    the `.` operator, since *s* is an expression rather than an
    identifier.

  - `builtins.getEnv` *s*  
    `getEnv` returns the value of the environment variable *s*, or an
    empty string if the variable doesn’t exist. This function should be
    used with care, as it can introduce all sorts of nasty environment
    dependencies in your Nix expression.
    
    `getEnv` is used in Nix Packages to locate the file
    `~/.nixpkgs/config.nix`, which contains user-local settings for Nix
    Packages. (That is, it does a `getEnv "HOME"` to locate the user’s
    home directory.)

  - `builtins.hasAttr` *s* *set*  
    `hasAttr` returns `true` if *set* has an attribute named *s*, and
    `false` otherwise. This is a dynamic version of the `?` operator,
    since *s* is an expression rather than an identifier.

  - `builtins.hashString` *type* *s*  
    Return a base-16 representation of the cryptographic hash of string
    *s*. The hash algorithm specified by *type* must be one of `"md5"`,
    `"sha1"`, `"sha256"` or `"sha512"`.

  - `builtins.hashFile` *type* *p*  
    Return a base-16 representation of the cryptographic hash of the
    file at path *p*. The hash algorithm specified by *type* must be one
    of `"md5"`, `"sha1"`, `"sha256"` or `"sha512"`.

  - `builtins.head` *list*  
    Return the first element of a list; abort evaluation if the argument
    isn’t a list or is an empty list. You can test whether a list is
    empty by comparing it with `[]`.

  - `import` *path*; `builtins.import` *path*  
    Load, parse and return the Nix expression in the file *path*. If
    *path* is a directory, the file ` default.nix
                     ` in that directory is loaded. Evaluation aborts if the file
    doesn’t exist or contains an incorrect Nix expression. `import`
    implements Nix’s module system: you can put any Nix expression (such
    as a set or a function) in a separate file, and use it from Nix
    expressions in other files.
    
    > **Note**
    > 
    > Unlike some languages, `import` is a regular function in Nix.
    > Paths using the angle bracket syntax (e.g., `
    >     >     >     >     > import` *\<foo\>*) are normal path values (see
    > [???](#ssec-values)).
    
    A Nix expression loaded by `import` must not contain any *free
    variables* (identifiers that are not defined in the Nix expression
    itself and are not built-in). Therefore, it cannot refer to
    variables that are in scope at the call site. For instance, if you
    have a calling expression
    
        rec {
          x = 123;
          y = import ./foo.nix;
        }
    
    then the following `foo.nix` will give an error:
    
        x + 456
    
    since `x` is not in scope in `foo.nix`. If you want `x` to be
    available in `foo.nix`, you should pass it as a function argument:
    
        rec {
          x = 123;
          y = import ./foo.nix x;
        }
    
    and
    
        x: x + 456
    
    (The function argument doesn’t have to be called `x` in `foo.nix`;
    any name would work.)

  - `builtins.intersectAttrs` *e1* *e2*  
    Return a set consisting of the attributes in the set *e2* that also
    exist in the set *e1*.

  - `builtins.isAttrs` *e*  
    Return `true` if *e* evaluates to a set, and `false` otherwise.

  - `builtins.isList` *e*  
    Return `true` if *e* evaluates to a list, and `false` otherwise.

  - `builtins.isFunction` *e*  
    Return `true` if *e* evaluates to a function, and `false` otherwise.

  - `builtins.isString` *e*  
    Return `true` if *e* evaluates to a string, and `false` otherwise.

  - `builtins.isInt` *e*  
    Return `true` if *e* evaluates to an int, and `false` otherwise.

  - `builtins.isFloat` *e*  
    Return `true` if *e* evaluates to a float, and `false` otherwise.

  - `builtins.isBool` *e*  
    Return `true` if *e* evaluates to a bool, and `false` otherwise.

  - `builtins.isPath` *e*  
    Return `true` if *e* evaluates to a path, and `false` otherwise.

  - `isNull` *e*; `builtins.isNull` *e*  
    Return `true` if *e* evaluates to `null`, and `false` otherwise.
    
    > **Warning**
    > 
    > This function is *deprecated*; just write `e == null` instead.

  - `builtins.length` *e*  
    Return the length of the list *e*.

  - `builtins.lessThan` *e1* *e2*  
    Return `true` if the number *e1* is less than the number *e2*, and
    `false` otherwise. Evaluation aborts if either *e1* or *e2* does not
    evaluate to a number.

  - `builtins.listToAttrs` *e*  
    Construct a set from a list specifying the names and values of each
    attribute. Each element of the list should be a set consisting of a
    string-valued attribute `name` specifying the name of the attribute,
    and an attribute `value` specifying its value. Example:
    
        builtins.listToAttrs
          [ { name = "foo"; value = 123; }
            { name = "bar"; value = 456; }
          ]
    
    evaluates to
    
        { foo = 123; bar = 456; }

  - `map` *f* *list*; `builtins.map` *f* *list*  
    Apply the function *f* to each element in the list *list*. For
    example,
    
        map (x: "foo" + x) [ "bar" "bla" "abc" ]
    
    evaluates to `[ "foobar" "foobla" "fooabc"
                    ]`.

  - `builtins.match` *regex* *str*  
    Returns a list if the [extended POSIX regular
    expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
    *regex* matches *str* precisely, otherwise returns `null`. Each item
    in the list is a regex group.
    
        builtins.match "ab" "abc"
    
    Evaluates to `null`.
    
        builtins.match "abc" "abc"
    
    Evaluates to `[ ]`.
    
        builtins.match "a(b)(c)" "abc"
    
    Evaluates to `[ "b" "c" ]`.
    
        builtins.match "[[:space:]]+([[:upper:]]+)[[:space:]]+" "  FOO   "
    
    Evaluates to `[ "foo" ]`.

  - `builtins.mul` *e1* *e2*  
    Return the product of the numbers *e1* and *e2*.

  - `builtins.parseDrvName` *s*  
    Split the string *s* into a package name and version. The package
    name is everything up to but not including the first dash followed
    by a digit, and the version is everything following that dash. The
    result is returned in a set `{ name, version }`. Thus,
    `builtins.parseDrvName "nix-0.12pre12876"` returns `{ name = "nix";
    version = "0.12pre12876";
                    }`.

  - `builtins.path` *args*  
    An enrichment of the built-in path type, based on the attributes
    present in *args*. All are optional except `path`:
    
      - path  
        The underlying path.
    
      - name  
        The name of the path when added to the store. This can used to
        reference paths that have nix-illegal characters in their names,
        like `@`.
    
      - filter  
        A function of the type expected by
        [builtins.filterSource](#builtin-filterSource), with the same
        semantics.
    
      - recursive  
        When `false`, when `path` is added to the store it is with a
        flat hash, rather than a hash of the NAR serialization of the
        file. Thus, `path` must refer to a regular file, not a
        directory. This allows similar behavior to `fetchurl`. Defaults
        to `true`.
    
      - sha256  
        When provided, this is the expected hash of the file at the
        path. Evaluation will fail if the hash is incorrect, and
        providing a hash allows `builtins.path` to be used even when the
        `pure-eval` nix config option is on.

  - `builtins.pathExists` *path*  
    Return `true` if the path *path* exists at evaluation time, and
    `false` otherwise.

  - `builtins.placeholder` *output*  
    Return a placeholder string for the specified *output* that will be
    substituted by the corresponding output path at build time. Typical
    outputs would be `"out"`, `"bin"` or `"dev"`.

  - `builtins.readDir` *path*  
    Return the contents of the directory *path* as a set mapping
    directory entries to the corresponding file type. For instance, if
    directory `A` contains a regular file `B` and another directory `C`,
    then `builtins.readDir
                    ./A` will return the set
    
        { B = "regular"; C = "directory"; }
    
    The possible values for the file type are `"regular"`,
    `"directory"`, `"symlink"` and `"unknown"`.

  - `builtins.readFile` *path*  
    Return the contents of the file *path* as a string.

  - `removeAttrs` *set* *list*; `builtins.removeAttrs` *set* *list*  
    Remove the attributes listed in *list* from *set*. The attributes
    don’t have to exist in *set*. For instance,
    
        removeAttrs { x = 1; y = 2; z = 3; } [ "a" "x" "z" ]
    
    evaluates to `{ y = 2; }`.

  - `builtins.replaceStrings` *from* *to* *s*  
    Given string *s*, replace every occurrence of the strings in *from*
    with the corresponding string in *to*. For example,
    
        builtins.replaceStrings ["oo" "a"] ["a" "i"] "foobar"
    
    evaluates to `"fabir"`.

  - `builtins.seq` *e1* *e2*  
    Evaluate *e1*, then evaluate and return *e2*. This ensures that a
    computation is strict in the value of *e1*.

  - `builtins.sort` *comparator* *list*  
    Return *list* in sorted order. It repeatedly calls the function
    *comparator* with two elements. The comparator should return `true`
    if the first element is less than the second, and `false` otherwise.
    For example,
    
        builtins.sort builtins.lessThan [ 483 249 526 147 42 77 ]
    
    produces the list `[ 42 77 147 249 483 526
                    ]`.
    
    This is a stable sort: it preserves the relative order of elements
    deemed equal by the comparator.

  - `builtins.split` *regex* *str*  
    Returns a list composed of non matched strings interleaved with the
    lists of the [extended POSIX regular
    expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
    *regex* matches of *str*. Each item in the lists of matched
    sequences is a regex group.
    
        builtins.split "(a)b" "abc"
    
    Evaluates to `[ "" [ "a" ] "c" ]`.
    
        builtins.split "([ac])" "abc"
    
    Evaluates to `[ "" [ "a" ] "b" [ "c" ] "" ]`.
    
        builtins.split "(a)|(c)" "abc"
    
    Evaluates to `[ "" [ "a" null ] "b" [ null "c" ] "" ]`.
    
        builtins.split "([[:upper:]]+)" "  FOO   "
    
    Evaluates to `[ " " [ "FOO" ] " " ]`.

  - `builtins.splitVersion` *s*  
    Split a string representing a version into its components, by the
    same version splitting logic underlying the version comparison in
    [`nix-env -u`](#ssec-version-comparisons).

  - `builtins.stringLength` *e*  
    Return the length of the string *e*. If *e* is not a string,
    evaluation is aborted.

  - `builtins.sub` *e1* *e2*  
    Return the difference between the numbers *e1* and *e2*.

  - `builtins.substring` *start* *len* *s*  
    Return the substring of *s* from character position *start*
    (zero-based) up to but not including *start + len*. If *start* is
    greater than the length of the string, an empty string is returned,
    and if *start + len* lies beyond the end of the string, only the
    substring up to the end of the string is returned. *start* must be
    non-negative. For example,
    
        builtins.substring 0 3 "nixos"
    
    evaluates to `"nix"`.

  - `builtins.tail` *list*  
    Return the second to last elements of a list; abort evaluation if
    the argument isn’t a list or is an empty list.

  - `throw` *s*; `builtins.throw` *s*  
    Throw an error message *s*. This usually aborts Nix expression
    evaluation, but in `nix-env -qa` and other commands that try to
    evaluate a set of derivations to get information about those
    derivations, a derivation that throws an error is silently skipped
    (which is not the case for `abort`).

  - `builtins.toFile` *name* *s*  
    Store the string *s* in a file in the Nix store and return its path.
    The file has suffix *name*. This file can be used as an input to
    derivations. One application is to write builders “inline”. For
    instance, the following Nix expression combines [???](#ex-hello-nix)
    and [???](#ex-hello-builder) into one file:
    
        { stdenv, fetchurl, perl }:
        
        stdenv.mkDerivation {
          name = "hello-2.1.1";
        
          builder = builtins.toFile "builder.sh" "
            source $stdenv/setup
        
            PATH=$perl/bin:$PATH
        
            tar xvfz $src
            cd hello-*
            ./configure --prefix=$out
            make
            make install
          ";
        
          src = fetchurl {
            url = "http://ftp.nluug.nl/pub/gnu/hello/hello-2.1.1.tar.gz";
            sha256 = "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465";
          };
          inherit perl;
        }
    
    It is even possible for one file to refer to another, e.g.,
    
    ``` 
      builder = let
        configFile = builtins.toFile "foo.conf" "
          # This is some dummy configuration file.
          ...
        ";
      in builtins.toFile "builder.sh" "
        source $stdenv/setup
        ...
        cp ${configFile} $out/etc/foo.conf
      ";
    ```
    
    Note that `${configFile}` is an antiquotation (see
    [???](#ssec-values)), so the result of the expression `configFile`
    (i.e., a path like `/nix/store/m7p7jfny445k...-foo.conf`) will be
    spliced into the resulting string.
    
    It is however *not* allowed to have files mutually referring to each
    other, like so:
    
        let
          foo = builtins.toFile "foo" "...${bar}...";
          bar = builtins.toFile "bar" "...${foo}...";
        in foo
    
    This is not allowed because it would cause a cyclic dependency in
    the computation of the cryptographic hashes for `foo` and `bar`.
    
    It is also not possible to reference the result of a derivation. If
    you are using Nixpkgs, the `writeTextFile` function is able to do
    that.

  - `builtins.toJSON` *e*  
    Return a string containing a JSON representation of *e*. Strings,
    integers, floats, booleans, nulls and lists are mapped to their JSON
    equivalents. Sets (except derivations) are represented as objects.
    Derivations are translated to a JSON string containing the
    derivation’s output path. Paths are copied to the store and
    represented as a JSON string of the resulting store path.

  - `builtins.toPath` *s*  
    DEPRECATED. Use `/. + "/path"` to convert a string into an absolute
    path. For relative paths, use `./. + "/path"`.

  - `toString` *e*; `builtins.toString` *e*  
    Convert the expression *e* to a string. *e* can be:
    
      - A string (in which case the string is returned unmodified).
    
      - A path (e.g., `toString /foo/bar` yields `"/foo/bar"`.
    
      - A set containing `{ __toString = self: ...; }`.
    
      - An integer.
    
      - A list, in which case the string representations of its elements
        are joined with spaces.
    
      - A Boolean (`false` yields `""`, `true` yields `"1"`).
    
      - `null`, which yields the empty string.

  - `builtins.toXML` *e*  
    Return a string containing an XML representation of *e*. The main
    application for `toXML` is to communicate information with the
    builder in a more structured format than plain environment
    variables.
    
    Here is an example where this is the case:
    
        { stdenv, fetchurl, libxslt, jira, uberwiki }:
        
        stdenv.mkDerivation (rec {
          name = "web-server";
        
          buildInputs = [ libxslt ];
        
          builder = builtins.toFile "builder.sh" "
            source $stdenv/setup
            mkdir $out
            echo "$servlets" | xsltproc ${stylesheet} - > $out/server-conf.xml ① 
          ";
        
          stylesheet = builtins.toFile "stylesheet.xsl" ② 
           "<?xml version='1.0' encoding='UTF-8'?>
            <xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>
              <xsl:template match='/'>
                <Configure>
                  <xsl:for-each select='/expr/list/attrs'>
                    <Call name='addWebApplication'>
                      <Arg><xsl:value-of select=\"attr[@name = 'path']/string/@value\" /></Arg>
                      <Arg><xsl:value-of select=\"attr[@name = 'war']/path/@value\" /></Arg>
                    </Call>
                  </xsl:for-each>
                </Configure>
              </xsl:template>
            </xsl:stylesheet>
          ";
        
          servlets = builtins.toXML [ ③ 
            { path = "/bugtracker"; war = jira + "/lib/atlassian-jira.war"; }
            { path = "/wiki"; war = uberwiki + "/uberwiki.war"; }
          ];
        })
    
    The builder is supposed to generate the configuration file for a
    [Jetty servlet container](http://jetty.mortbay.org/). A servlet
    container contains a number of servlets (`*.war` files) each
    exported under a specific URI prefix. So the servlet configuration
    is a list of sets containing the `path` and `war` of the servlet
    ([???](#ex-toxml-co-servlets)). This kind of information is
    difficult to communicate with the normal method of passing
    information through an environment variable, which just concatenates
    everything together into a string (which might just work in this
    case, but wouldn’t work if fields are optional or contain lists
    themselves). Instead the Nix expression is converted to an XML
    representation with `toXML`, which is unambiguous and can easily be
    processed with the appropriate tools. For instance, in the example
    an XSLT stylesheet (at point ②) is applied to it (at point ①) to
    generate the XML configuration file for the Jetty server. The XML
    representation produced at point ③ by `toXML` is as follows:
    
        <?xml version='1.0' encoding='utf-8'?>
        <expr>
          <list>
            <attrs>
              <attr name="path">
                <string value="/bugtracker" />
              </attr>
              <attr name="war">
                <path value="/nix/store/d1jh9pasa7k2...-jira/lib/atlassian-jira.war" />
              </attr>
            </attrs>
            <attrs>
              <attr name="path">
                <string value="/wiki" />
              </attr>
              <attr name="war">
                <path value="/nix/store/y6423b1yi4sx...-uberwiki/uberwiki.war" />
              </attr>
            </attrs>
          </list>
        </expr>
    
    Note that [???](#ex-toxml) uses the `toFile` built-in to write the
    builder and the stylesheet “inline” in the Nix expression. The path
    of the stylesheet is spliced into the builder using the syntax
    `xsltproc ${stylesheet}`.

  - `builtins.trace` *e1* *e2*  
    Evaluate *e1* and print its abstract syntax representation on
    standard error. Then return *e2*. This function is useful for
    debugging.

  - `builtins.tryEval` *e*  
    Try to shallowly evaluate *e*. Return a set containing the
    attributes `success` (`true` if *e* evaluated successfully, `false`
    if an error was thrown) and `value`, equalling *e* if successful and
    `false` otherwise. Note that this doesn't evaluate *e* deeply, so
    ` let e = { x = throw ""; }; in (builtins.tryEval e).success
                     ` will be `true`. Using ` builtins.deepSeq
                     ` one can get the expected result: `let e = { x = throw "";
                    }; in (builtins.tryEval (builtins.deepSeq e e)).success` will be
    `false`.

  - `builtins.typeOf` *e*  
    Return a string representing the type of the value *e*, namely
    `"int"`, `"bool"`, `"string"`, `"path"`, `"null"`, `"set"`,
    `"list"`, `"lambda"` or `"float"`.
