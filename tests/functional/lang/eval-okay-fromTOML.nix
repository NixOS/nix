[

  (builtins.fromTOML ''
    # This is a TOML document.

    title = "TOML Example"

    [owner]
    name = "Tom Preston-Werner"
    #dob = 1979-05-27T07:32:00-08:00 # First class dates

    [database]
    server = "192.168.1.1"
    ports = [ 8001, 8001, 8002 ]
    connection_max = 5000
    enabled = true

    [servers]

      # Indentation (tabs and/or spaces) is allowed but not required
      [servers.alpha]
      ip = "10.0.0.1"
      dc = "eqdc10"

      [servers.beta]
      ip = "10.0.0.2"
      dc = "eqdc10"

    [clients]
    data = [ ["gamma", "delta"], [1, 2] ]

    # Line breaks are OK when inside arrays
    hosts = [
      "alpha",
      "omega"
    ]
  '')

  (builtins.fromTOML ''
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

    # FIXME: not supported because Nix doesn't have a date/time type.
    #odt1 = 1979-05-27T07:32:00Z
    #odt2 = 1979-05-27T00:32:00-07:00
    #odt3 = 1979-05-27T00:32:00.999999-07:00
    #odt4 = 1979-05-27 07:32:00Z
    #ldt1 = 1979-05-27T07:32:00
    #ldt2 = 1979-05-27T00:32:00.999999
    #ld1 = 1979-05-27
    #lt1 = 07:32:00
    #lt2 = 00:32:00.999999

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
  '')

  (builtins.fromTOML ''
    [[package]]
    name = "aho-corasick"
    version = "0.6.4"
    source = "registry+https://github.com/rust-lang/crates.io-index"
    dependencies = [
     "memchr 2.0.1 (registry+https://github.com/rust-lang/crates.io-index)",
    ]

    [[package]]
    name = "ansi_term"
    version = "0.9.0"
    source = "registry+https://github.com/rust-lang/crates.io-index"

    [[package]]
    name = "atty"
    version = "0.2.10"
    source = "registry+https://github.com/rust-lang/crates.io-index"
    dependencies = [
     "libc 0.2.42 (registry+https://github.com/rust-lang/crates.io-index)",
     "termion 1.5.1 (registry+https://github.com/rust-lang/crates.io-index)",
     "winapi 0.3.5 (registry+https://github.com/rust-lang/crates.io-index)",
    ]

    [metadata]
    "checksum aho-corasick 0.6.4 (registry+https://github.com/rust-lang/crates.io-index)" = "d6531d44de723825aa81398a6415283229725a00fa30713812ab9323faa82fc4"
    "checksum ansi_term 0.11.0 (registry+https://github.com/rust-lang/crates.io-index)" = "ee49baf6cb617b853aa8d93bf420db2383fab46d314482ca2803b40d5fde979b"
    "checksum ansi_term 0.9.0 (registry+https://github.com/rust-lang/crates.io-index)" = "23ac7c30002a5accbf7e8987d0632fa6de155b7c3d39d0067317a391e00a2ef6"
    "checksum arrayvec 0.4.7 (registry+https://github.com/rust-lang/crates.io-index)" = "a1e964f9e24d588183fcb43503abda40d288c8657dfc27311516ce2f05675aef"
  '')

  (builtins.fromTOML ''
    a = [[{ b = true }]]
    c = [ [ { d = true } ] ]
    e = [[123]]
  '')

]
