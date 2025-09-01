builtins.fromTOML ''
  key = "value"
  bare_key = "value"
  bare-key = "value"
  1234 = "value"

  "127.0.0.1" = "value"
  "character encoding" = "value"
  "ʎǝʞ" = "value"
  'key2' = "value"
  'quoted "value"' = "value"

  name = "Orange"

  physical.color = "orange"
  physical.shape = "round"
  site."google.com" = true

  # This is legal according to the spec, but cpptoml doesn't handle it.
  #a.b.c = 1
  #a.d = 2

  str = "I'm a string. \"You can quote me\". Name\tJos\u00E9\nLocation\tSF."

  int1 = +99
  int2 = 42
  int3 = 0
  int4 = -17
  int5 = 1_000
  int6 = 5_349_221
  int7 = 1_2_3_4_5

  hex1 = 0xDEADBEEF
  hex2 = 0xdeadbeef
  hex3 = 0xdead_beef

  oct1 = 0o01234567
  oct2 = 0o755

  bin1 = 0b11010110

  flt1 = +1.0
  flt2 = 3.1415
  flt3 = -0.01
  flt4 = 5e+22
  flt5 = 1e6
  flt6 = -2E-2
  flt7 = 6.626e-34
  flt8 = 9_224_617.445_991_228_313

  bool1 = true
  bool2 = false

  odt1 = 1979-05-27T07:32:00Z
  odt2 = 1979-05-27T00:32:00-07:00
  odt3 = 1979-05-27T00:32:00.999999-07:00
  odt4 = 1979-05-27 07:32:00Z
  # milliseconds
  odt5 = 1979-05-27 07:32:00.1Z
  odt6 = 1979-05-27 07:32:00.12Z
  odt7 = 1979-05-27 07:32:00.123Z
  # microseconds
  odt8 = 1979-05-27t07:32:00.1234Z
  odt9 = 1979-05-27t07:32:00.12345Z
  odt10 = 1979-05-27t07:32:00.123456Z
  # nanoseconds
  odt11 = 1979-05-27 07:32:00.1234567Z
  odt12 = 1979-05-27 07:32:00.12345678Z
  odt13 = 1979-05-27 07:32:00.123456789Z
  # no more precision after nanoseconds
  odt14 = 1979-05-27t07:32:00.1234567891Z

  ldt1 = 1979-05-27T07:32:00
  # milliseconds
  ldt2 = 1979-05-27T07:32:00.1
  ldt3 = 1979-05-27T07:32:00.12
  ldt4 = 1979-05-27T07:32:00.123
  # microseconds
  ldt5 = 1979-05-27t00:32:00.1234
  ldt6 = 1979-05-27t00:32:00.12345
  ldt7 = 1979-05-27t00:32:00.123456
  # nanoseconds
  ldt8 = 1979-05-27 00:32:00.1234567
  ldt9 = 1979-05-27 00:32:00.12345678
  ldt10 = 1979-05-27 00:32:00.123456789
  # no more precision after nanoseconds
  ldt11 = 1979-05-27t00:32:00.1234567891

  ld1 = 1979-05-27
  lt1 = 07:32:00
  # milliseconds
  lt2 = 00:32:00.1
  lt3 = 00:32:00.12
  lt4 = 00:32:00.123
  # microseconds
  lt5 = 00:32:00.1234
  lt6 = 00:32:00.12345
  lt7 = 00:32:00.123456
  # nanoseconds
  lt8 = 00:32:00.1234567
  lt9 = 00:32:00.12345678
  lt10 = 00:32:00.123456789
  # no more precision after nanoseconds
  lt11 = 00:32:00.1234567891

  arr1 = [ 1, 2, 3 ]
  arr2 = [ "red", "yellow", "green" ]
  arr3 = [ [ 1, 2 ], [3, 4, 5] ]
  arr4 = [ "all", 'strings', """are the same""", ''''type'''']
  arr5 = [ [ 1, 2 ], ["a", "b", "c"] ]

  arr7 = [
    1, 2, 3
  ]

  arr8 = [
    1,
    2, # this is ok
  ]

  [table-1]
  key1 = "some string"
  key2 = 123


  [table-2]
  key1 = "another string"
  key2 = 456

  [dog."tater.man"]
  type.name = "pug"

  [a.b.c]
  [ d.e.f ]
  [ g .  h  . i ]
  [ j . "ʞ" . 'l' ]
  [x.y.z.w]

  name = { first = "Tom", last = "Preston-Werner" }
  point = { x = 1, y = 2 }
  animal = { type.name = "pug" }

  [[products]]
  name = "Hammer"
  sku = 738594937

  [[products]]

  [[products]]
  name = "Nail"
  sku = 284758393
  color = "gray"

  [[fruit]]
    name = "apple"

    [fruit.physical]
      color = "red"
      shape = "round"

    [[fruit.variety]]
      name = "red delicious"

    [[fruit.variety]]
      name = "granny smith"

  [[fruit]]
    name = "banana"

    [[fruit.variety]]
      name = "plantain"
''
