/*
  Contains many example for doc-comments
  From basic to more complex problems.
*/
rec {

  #############################################

  # Parenthesis

  /**Doc*/
  f = (( ( ( a: 1))));

  #############################################

  # Long Documentation

  /**
  # Return an attribute from nested attribute sets.

  # Example

    x = { a = { b = 3; }; }
    # ["a" "b"] is equivalent to x.a.b
    # 6 is a default value to return if the path does not exist in attrset
    attrByPath ["a" "b"] 6 x
    => 3
    attrByPath ["z" "z"] 6 x
    => 6

  # Type
    attrByPath :: [String] -> Any -> AttrSet -> Any

  */
  map = x: x;

  /**
    Like builtins.foldl' but for attribute sets.
    Iterates over every name-value pair in the given attribute set.
    The result of the callback function is often called `acc` for accumulator. It is passed between callbacks from left to right and the final `acc` is the return value of `foldlAttrs`.

    Attention:
    There is a completely different function
    `lib.foldAttrs`
    which has nothing to do with this function, despite the similar name.

    # Example

    ```nix
    foldlAttrs
      (acc: name: value: {
        sum = acc.sum + value;
        names = acc.names ++ [name];
      })
      { sum = 0; names = []; }
      {
        foo = 1;
        bar = 10;
      }
    ->
      {
        sum = 11;
        names = ["bar" "foo"];
      }

    foldlAttrs
      (throw "function not needed")
      123
      {};
    ->
      123

    foldlAttrs
      (_: _: v: v)
      (throw "initial accumulator not needed")
      { z = 3; a = 2; };
    ->
      3

    The accumulator doesn't have to be an attrset.
    It can be as simple as a number or string.

    foldlAttrs
      (acc: _: v: acc * 10 + v)
      1
      { z = 1; a = 2; };
    ->
      121
    ```

    # Type

    ```
    foldlAttrs :: ( a -> String -> b -> a ) -> a -> { ... :: b } -> a
    ```
  */
  foldlAttrs = f: init: set:
    builtins.foldl'
      (acc: name: f acc name set.${name})
      init
      (builtins.attrNames set);

  /**Dense docs*/name=x: x;


  ident = builtins.indent or (x: x);

  one = ident 1;

  error = rec {
    expr = builtins.unsafeGetLambdaDoc ({
        /**
        Foo docs
        */
        foo = a: b: c: a;
      }.foo "1");
    expected = {
      content = "Foo docs";
      isPrimop = false;
      position = expr.position;
    };
  };


  /**
   This is a deprecated function aliasing the new one
  */
  deprecatedMap = map;

  foo =
    builtins.unsafeGetLambdaDoc
    {
      /**
      # The id function

      * Bullet item
      * another item

      ## h2 markdown heading

      some more docs
      */
      foo = x: x;
    }
    .foo;

  /**
    Function behind a path
  */
  a.b = a: {b ? null}: a b;

  /**
    Doc 2 (should not be shown
  */
  fn =
    /**
    Doc 1
    */
    f: (x: f + x);

  /**
    Docs
  */
  attrDoc = 1;

  lambdaDoc =
    builtins.id
    or
    /**
      Docs
    */
    (x: x);


  /*
    With this implementation you can also use 'unsafeGetAttrDoc' on 'functionArgs fn'
    Although this was unintenional it is suprsingly powerfull.

    Example
    let
      args = functionArgs mkDerivation;
    in
      mapAttrs (n: _: unsafeGetAttrDoc n args) args;
    =>
    {
      outputs = <docs>;
      buildPhase = <docs>;
    }

    > Note: In practice this wont work on mkDerivation because mkDerivation is non discoverable: `functionArgs pkgs.stdenv.mkDerivation => { }`
  */
  mkDerivation = {
    /**
    Output Docs
    */
    outputs,
    /**
    BuildPhase Docs
    */
    buildPhase,
  }:
    derivation;

  /**
  Docs
  */
  specialLambdaDoc = z: ({
    a,
    v,
  }: (x: x));


  /**
    Specialization of fn
  */
  alias = fn 1;

  ###################################

  # NOT SUPPORTED

  /**
    Doc Comment with complex path
  */
  ${"id"} = x: x;

  /**ignoreFour*/
  ignoreFour = a: b: c: d: d;

  /**some Id*/
  someId = ignoreFour 1 1 1;
  /**Double Id*/
  doubleIt = ignoreFour 1 1;
  /**
    Doc Comment with an argument pattern
  */
  patFn = { a ? 1 }: a;
}
