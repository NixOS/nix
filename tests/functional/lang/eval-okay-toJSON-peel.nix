# Characterise `builtins.toJSON`'s peeling of `__toString` and `outPath` attrs
#
# You'll see quirky behaviors acknowledged with a comment, but those are no
# excuse to start making breaking changes.
{
  outPathString = builtins.toJSON { outPath = "hello"; };

  # Quirky: just 42
  outPathInt = builtins.toJSON { outPath = 42; };

  outPathNested = builtins.toJSON {
    outPath = {
      outPath = "nested";
    };
  };

  # Quirky: just the inner attrs
  outPathDeadEnd = builtins.toJSON {
    outPath = {
      a = 1;
      b = 2;
    };
  };

  toStringPrecedes = builtins.toJSON {
    __toString = self: "won";
    # Show that it's unused
    outPath = abort "unreached";
  };

  toStringReturnsPeelableAttrs = builtins.toJSON {
    __toString = self: { outPath = "deep"; };
  };

  plainAttrs = builtins.toJSON {
    a = 1;
    b = "two";
  };
}
