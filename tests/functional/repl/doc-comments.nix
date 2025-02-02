{
  /**
    Perform *arithmetic* multiplication. It's kind of like repeated **addition**, very neat.

    ```nix
    multiply 2 3
    => 6
    ```
  */
  multiply = x: y: x * y;

  /**
    👈 precisely this wide 👉
  */
  measurement = x: x;

  floatedIn =
    /**
      This also works.
    */
    x: y: x;

  compact =
    /**
      boom
    */
    x: x;

  # https://github.com/NixOS/rfcs/blob/master/rfcs/0145-doc-strings.md#ambiguous-placement
  /**
    Ignore!!!
  */
  unambiguous =
    /**
      Very close
    */
    x: x;

  /**
    Firmly rigid.
  */
  constant = true;

  /**
    Immovably fixed.
  */
  lib.version = "9000";

  /**
    Unchangeably constant.
  */
  lib.attr.empty = { };

  lib.attr.undocumented = { };

  nonStrict =
    /**
      My syntax is not strict, but I'm strict anyway.
    */
    x: x;
  strict =
    /**
      I don't have to be strict, but I am anyway.
    */
    { ... }: null;
  # Note that pre and post are the same here. I just had to name them somehow.
  strictPre =
    /**
      Here's one way to do this
    */
    a@{ ... }: a;
  strictPost =
    /**
      Here's another way to do this
    */
    { ... }@a: a;

  # TODO

  /**
    You won't see this.
  */
  curriedArgs =
    /**
      A documented function.
    */
    x:
    /**
      The function returned by applying once
    */
    y:
    /**
      A function body performing summation of two items
    */
    x + y;

  /**
    Documented formals (but you won't see this comment)
  */
  documentedFormals =
    /**
      Finds x
    */
    {
      /**
        The x attribute
      */
      x,
    }:
    x;
}
