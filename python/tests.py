import nix
import unittest


class TestPythonNix(unittest.TestCase):
    def test_dict(self):
        val = dict(a=1)
        self.assertEqual(nix.eval("a", vars=dict(a=val)), val)

    def test_string(self):
        self.assertEqual(nix.eval("a", vars=dict(a="foo")), "foo")

    def test_bool(self):
        self.assertEqual(nix.eval("a", vars=dict(a=True)), True)

    def test_none(self):
        self.assertEqual(nix.eval("a", vars=dict(a=None)), None)

    def test_ifd(self):
        expression = """
        builtins.readFile (derivation {
          name = "test";
          args = [ "-c" "printf \\"%s\\" test > $out" ];
          builder = "/bin/sh";
          system = builtins.currentSystem;
        })
        """
        self.assertEqual(nix.eval(expression, vars=dict()), "test")

    def test_throw(self):
        errorString = "hello hi there\ntest"
        try:
            nix.eval("throw str", vars=dict(str=errorString))
        except nix.NixError as e:
            self.assertEqual(e.args[0], errorString)

    # This test case fails if you uncomment the `Py_{BEGIN,END}_ALLOW_THREADS`
    # macros in src/eval.cc
    def test_GIL_case(self):
        try:
            nix.eval("{ a = throw \"nope\"; }")
        except nix.NixError as e:
            self.assertEqual(e.args[0], "nope")

if __name__ == '__main__':
    unittest.main()
