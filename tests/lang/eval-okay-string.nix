"foo" + "bar"
  + toString 42.0
  + toString (/a/b + /c/d)
  + toString 0.00000000023
  + toString (/foo/bar + "/../xyzzy/." + "/foo.txt")
  + toString 1.1e20
  + ("/../foo" + toString /x/y)
  + toString 1.00000000000001
  + "escape: \"quote\" \n \\"
  + "end
of
line"
  + "foo${if true then "b${"a" + "r"}" else "xyzzy"}blaat"
  + "foo$bar"
  + "$\"$\""
  + "$"
