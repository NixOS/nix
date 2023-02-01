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

if __name__ == '__main__':
    unittest.main()
