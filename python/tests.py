import nix
import unittest


class TestPythonNix(unittest.TestCase):
    def test_dict(self):
        val = dict(a=1)
        self.assertEqual(nix.callExprString("{ a }: a", arg=dict(a=val)), val)

    def test_string(self):
        self.assertEqual(nix.callExprString("{ a }: a", arg=dict(a="foo")), "foo")

    def test_bool(self):
        self.assertEqual(nix.callExprString("{ a }: a", arg=dict(a=True)), True)

    def test_none(self):
        self.assertEqual(nix.callExprString("{ a }: a", arg=dict(a=None)), None)

    def test_ifd(self):
        expression = """
        {}:
        builtins.readFile (derivation {
          name = "test";
          args = [ "-c" "printf \\"%s\\" test > $out" ];
          builder = "/bin/sh";
          system = builtins.currentSystem;
        })
        """
        self.assertEqual(nix.callExprString(expression, arg={}), "test")

    def test_throw(self):
        errorString = "hello hi there\ntest"
        with self.assertRaises(nix.ThrownNixError) as cm:
            nix.callExprString("{ str }: throw str", arg=dict(str=errorString))
        self.assertEqual(cm.exception.args[0], errorString)

    def test_syntax_error(self):
        with self.assertRaises(nix.NixError) as cm:
            nix.callExprString("{", arg={})

    def test_GIL_case(self):
        with self.assertRaises(nix.ThrownNixError) as cm:
            nix.callExprString("{}: { a = throw \"nope\"; }", arg={})
        self.assertEqual(cm.exception.args[0], "nope")

    def test_infinity(self):
        with self.assertRaises(nix.NixError):
            nix.callExprString("{}: let x = { inherit x; }; in x", arg={})

    def test_null_expression(self):
        # Null characters should be allowed in expressions, even if they aren't
        # very useful really, though at least null's should be supported in
        # strings in the future https://github.com/NixOS/nix/issues/1307)
        self.assertEqual(nix.callExprString("{}: \"ab\x00cd\"", arg={}), "ab")

    def test_throw_null(self):
        with self.assertRaises(nix.ThrownNixError) as cm:
            nix.callExprString("{}: throw \"hello\x00there\"", arg={})
        self.assertEqual(cm.exception.args[0], "hello")

    def test_booleans(self):
        self.assertIs(nix.callExprString("{ a }: assert a == true; a", arg=dict(a=True)), True)
        self.assertIs(nix.callExprString("{ a }: assert a == false; a", arg=dict(a=False)), False)

    def test_null(self):
        self.assertIs(nix.callExprString("{ a }: assert a == null; a", arg=dict(a=None)), None)

if __name__ == '__main__':
    unittest.main()
