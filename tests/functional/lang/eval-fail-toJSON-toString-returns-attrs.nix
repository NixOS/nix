# toJSON checks the return type of __toString
builtins.toJSON { __toString = self: { a = 1; }; }
