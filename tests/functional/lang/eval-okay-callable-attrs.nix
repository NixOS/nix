({ __functor = self: x: self.foo && x; foo = false; } // { foo = true; }) true
