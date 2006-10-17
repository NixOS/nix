"foo" + "bar"
  + toString (/a/b + /c/d)
  + toString (/foo/bar + "/../xyzzy/." + "/foo.txt")
  + ("/../foo" + toString /x/y)
  + "escape: \"quote\" \n \\"
  + "end
of
line"
  + "foo${if true then "b${"a" + "r"}" else "xyzzy"}blaat"
  + "foo$bar"
