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

    # This test case fails if you uncomment the `Py_{BEGIN,END}_ALLOW_THREADS`
    # macros in src/eval.cc
    def test_GIL_case(self):
        nix.eval("{ a = throw \"nope\"; }")

if __name__ == '__main__':
    unittest.main()
