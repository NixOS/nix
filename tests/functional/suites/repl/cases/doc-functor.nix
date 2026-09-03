rec {
  /**
    Look, it's just like a function!
  */
  multiply = p: q: p * q;

  multiplier = {
    factor = 2;
    /**
      Multiply the argument by the factor stored in the factor attribute.
    */
    __functor = self: x: x * self.factor;
  };

  doubler = {
    description = "bla";
    /**
      Multiply by two. This doc probably won't be rendered because the
      returned partial application won't have any reference to this location;
      only pointing to the second lambda in the multiply function.
    */
    __functor = self: multiply 2;
  };

  makeOverridable = f: {
    /**
      This is a function that can be overridden.
    */
    __functor = self: f;
    override = throw "not implemented";
  };

  /**
    Compute x^2
  */
  square = x: x * x;

  helper = makeOverridable square;

  # Somewhat analogous to the Nixpkgs makeOverridable function.
  makeVeryOverridable = f: {
    /**
      This is a function that can be overridden.
    */
    __functor =
      self: arg:
      f arg
      // {
        override = throw "not implemented";
        overrideAttrs = throw "not implemented";
      };
    override = throw "not implemented";
  };

  helper2 = makeVeryOverridable square;

  # The RFC might be ambiguous here. The doc comment from makeVeryOverridable
  # is "inner" in terms of values, but not inner in terms of expressions.
  # Returning the following attribute comment might be allowed.
  # TODO: I suppose we could look whether the attribute value expression
  #       contains a doc, and if not, return the attribute comment anyway?

  /**
    Compute x^3
  */
  lib.helper3 = makeVeryOverridable (x: x * x * x);

  /**
    Compute x^3...
  */
  helper3 = makeVeryOverridable (x: x * x * x);

  # ------

  # getDoc traverses a potentially infinite structure in case of __functor, so
  # we need to test with recursive inputs and diverging inputs.

  recursive = {
    /**
      This looks bad, but the docs are ok because of the eta expansion.
    */
    __functor = self: x: self x;
  };

  recursive2 = {
    /**
      Docs probably won't work in this case, because the "partial" application
      of self results in an infinite recursion.
    */
    __functor = self: self.__functor self;
  };

  diverging =
    let
      /**
        Docs probably won't work in this case, because the "partial" application
        of self results in an diverging computation that causes a stack overflow.
        It's not an infinite recursion because each call is different.
        This must be handled by the documentation retrieval logic, as it
        reimplements the __functor invocation to be partial.
      */
      f = x: {
        __functor = self: (f (x + 1));
      };
    in
    f null;

}
