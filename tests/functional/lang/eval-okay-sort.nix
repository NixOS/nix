with builtins;

[
  (sort lessThan [
    483
    249
    526
    147
    42
    77
  ])
  (sort (x: y: y < x) [
    483
    249
    526
    147
    42
    77
  ])
  (sort lessThan [
    "foo"
    "bar"
    "xyzzy"
    "fnord"
  ])
  (sort (x: y: x.key < y.key) [
    {
      key = 1;
      value = "foo";
    }
    {
      key = 2;
      value = "bar";
    }
    {
      key = 1;
      value = "fnord";
    }
  ])
  (sort (x: y: x.key < y.key) [
    {
      key = 1;
      value = "foo";
    }
    {
      key = 2;
      value = "bar";
    }
    {
      key = 1;
      value = "foo2";
    }
    {
      key = 2;
      value = "bar2";
    }
    {
      key = 2;
      value = "bar3";
    }
    {
      key = 2;
      value = "bar4";
    }
    {
      key = 1;
      value = "foo3";
    }
    {
      key = 3;
      value = "baz";
    }
    {
      key = 3;
      value = "baz2";
    }
    {
      key = 1;
      value = "foo4";
    }
    {
      key = 3;
      value = "baz3";
    }
    {
      key = 1;
      value = "foo5";
    }
    {
      key = 1;
      value = "foo6";
    }
    {
      key = 2;
      value = "bar5";
    }
    {
      key = 3;
      value = "baz4";
    }
    {
      key = 1;
      value = "foo7";
    }
    {
      key = 4;
      value = "biz1";
    }
    {
      key = 1;
      value = "foo8";
    }
  ])
  (sort lessThan [
    [
      1
      6
    ]
    [ ]
    [
      2
      3
    ]
    [ 3 ]
    [
      1
      5
    ]
    [ 2 ]
    [ 1 ]
    [ ]
    [
      1
      4
    ]
    [ 3 ]
  ])
]
