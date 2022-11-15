#pragma once


static constexpr std::string_view T_229Q = R"RAW(
---
- name: Spec Example 2.4. Sequence of Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2760193
  tags: sequence mapping spec
  yaml: |
    -
      name: Mark McGwire
      hr:   65
      avg:  0.278
    -
      name: Sammy Sosa
      hr:   63
      avg:  0.288
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :name
        =VAL :Mark McGwire
        =VAL :hr
        =VAL :65
        =VAL :avg
        =VAL :0.278
       -MAP
       +MAP
        =VAL :name
        =VAL :Sammy Sosa
        =VAL :hr
        =VAL :63
        =VAL :avg
        =VAL :0.288
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "name": "Mark McGwire",
        "hr": 65,
        "avg": 0.278
      },
      {
        "name": "Sammy Sosa",
        "hr": 63,
        "avg": 0.288
      }
    ]
  dump: |
    - name: Mark McGwire
      hr: 65
      avg: 0.278
    - name: Sammy Sosa
      hr: 63
      avg: 0.288
)RAW";

static constexpr std::string_view T_236B = R"RAW(
---
- name: Invalid value after mapping
  from: '@perlpunk'
  tags: error mapping
  fail: true
  yaml: |
    foo:
      bar
    invalid
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :bar
)RAW";

static constexpr std::string_view T_26DV = R"RAW(
---
- name: Whitespace around colon in mappings
  from: '@perlpunk'
  tags: alias mapping whitespace
  yaml: |
    "top1" :␣
      "key1" : &alias1 scalar1
    'top2' :␣
      'key2' : &alias2 scalar2
    top3: &node3␣
      *alias1 : scalar3
    top4:␣
      *alias2 : scalar4
    top5   :␣␣␣␣
      scalar5
    top6:␣
      &anchor6 'key6' : scalar6
  tree: |
    +STR
     +DOC
      +MAP
       =VAL "top1
       +MAP
        =VAL "key1
        =VAL &alias1 :scalar1
       -MAP
       =VAL 'top2
       +MAP
        =VAL 'key2
        =VAL &alias2 :scalar2
       -MAP
       =VAL :top3
       +MAP &node3
        =ALI *alias1
        =VAL :scalar3
       -MAP
       =VAL :top4
       +MAP
        =ALI *alias2
        =VAL :scalar4
       -MAP
       =VAL :top5
       =VAL :scalar5
       =VAL :top6
       +MAP
        =VAL &anchor6 'key6
        =VAL :scalar6
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "top1": {
        "key1": "scalar1"
      },
      "top2": {
        "key2": "scalar2"
      },
      "top3": {
        "scalar1": "scalar3"
      },
      "top4": {
        "scalar2": "scalar4"
      },
      "top5": "scalar5",
      "top6": {
        "key6": "scalar6"
      }
    }
  dump: |
    "top1":
      "key1": &alias1 scalar1
    'top2':
      'key2': &alias2 scalar2
    top3: &node3
      *alias1 : scalar3
    top4:
      *alias2 : scalar4
    top5: scalar5
    top6:
      &anchor6 'key6': scalar6
)RAW";

static constexpr std::string_view T_27NA = R"RAW(
---
- name: Spec Example 5.9. Directive Indicator
  from: http://www.yaml.org/spec/1.2/spec.html#id2774058
  tags: spec directive 1.3-err
  yaml: |
    %YAML 1.2
    --- text
  tree: |
    +STR
     +DOC ---
      =VAL :text
     -DOC
    -STR
  json: |
    "text"
  dump: |
    --- text
)RAW";

static constexpr std::string_view T_2AUY = R"RAW(
---
- name: Tags in Block Sequence
  from: NimYAML tests
  tags: tag sequence
  yaml: |2
     - !!str a
     - b
     - !!int 42
     - d
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL <tag:yaml.org,2002:str> :a
       =VAL :b
       =VAL <tag:yaml.org,2002:int> :42
       =VAL :d
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a",
      "b",
      42,
      "d"
    ]
  dump: |
    - !!str a
    - b
    - !!int 42
    - d
)RAW";

static constexpr std::string_view T_2CMS = R"RAW(
---
- name: Invalid mapping in plain multiline
  from: '@perlpunk'
  tags: error mapping
  fail: true
  yaml: |
    this
     is
      invalid: x
  tree: |
    +STR
     +DOC
)RAW";

static constexpr std::string_view T_2EBW = R"RAW(
---
- name: Allowed characters in keys
  from: '@perlpunk'
  tags: mapping scalar
  yaml: |
    a!"#$%&'()*+,-./09:;<=>?@AZ[\]^_`az{|}~: safe
    ?foo: safe question mark
    :foo: safe colon
    -foo: safe dash
    this is#not: a comment
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a!"#$%&'()*+,-./09:;<=>?@AZ[\\]^_`az{|}~
       =VAL :safe
       =VAL :?foo
       =VAL :safe question mark
       =VAL ::foo
       =VAL :safe colon
       =VAL :-foo
       =VAL :safe dash
       =VAL :this is#not
       =VAL :a comment
      -MAP
     -DOC
    -STR
  json: |
    {
      "a!\"#$%&'()*+,-./09:;<=>?@AZ[\\]^_`az{|}~": "safe",
      "?foo": "safe question mark",
      ":foo": "safe colon",
      "-foo": "safe dash",
      "this is#not": "a comment"
    }
  dump: |
    a!"#$%&'()*+,-./09:;<=>?@AZ[\]^_`az{|}~: safe
    ?foo: safe question mark
    :foo: safe colon
    -foo: safe dash
    this is#not: a comment
)RAW";

static constexpr std::string_view T_2G84 = R"RAW(
---
- name: Literal modifers
  from: '@ingydotnet'
  tags: literal scalar
  fail: true
  yaml: |
    --- |0
  tree: |
    +STR
     +DOC ---

- fail: true
  yaml: |
    --- |10

- yaml: |
    --- |1-∎
  tree: |
    +STR
     +DOC ---
      =VAL |
     -DOC
    -STR
  json: |
    ""
  emit: |
    --- ""

- yaml: |
    --- |1+∎
  tree: |
    +STR
     +DOC ---
      =VAL |
     -DOC
    -STR
  emit: |
    --- ""
)RAW";

static constexpr std::string_view T_2JQS = R"RAW(
---
- name: Block Mapping with Missing Keys
  from: NimYAML tests
  tags: duplicate-key mapping empty-key
  yaml: |
    : a
    : b
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :
       =VAL :a
       =VAL :
       =VAL :b
      -MAP
     -DOC
    -STR
)RAW";

static constexpr std::string_view T_2LFX = R"RAW(
---
- name: Spec Example 6.13. Reserved Directives [1.3]
  from: 6LVF, modified for YAML 1.3
  tags: spec directive header double 1.3-mod
  yaml: |
    %FOO  bar baz # Should be ignored
                  # with a warning.
    ---
    "foo"
  tree: |
    +STR
     +DOC ---
      =VAL "foo
     -DOC
    -STR
  json: |
    "foo"
  dump: |
    ---
    "foo"
  emit: |
    --- "foo"
)RAW";

static constexpr std::string_view T_2SXE = R"RAW(
---
- name: Anchors With Colon in Name
  from: Mailing List Discussion
  tags: alias edge mapping 1.3-err
  yaml: |
    &a: key: &a value
    foo:
      *a:
  tree: |
    +STR
     +DOC
      +MAP
       =VAL &a: :key
       =VAL &a :value
       =VAL :foo
       =ALI *a:
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": "value",
      "foo": "key"
    }
  dump: |
    &a: key: &a value
    foo: *a:
)RAW";

static constexpr std::string_view T_2XXW = R"RAW(
---
- name: Spec Example 2.25. Unordered Sets
  from: http://www.yaml.org/spec/1.2/spec.html#id2761758
  tags: spec mapping unknown-tag explicit-key
  yaml: |
    # Sets are represented as a
    # Mapping where each key is
    # associated with a null value
    --- !!set
    ? Mark McGwire
    ? Sammy Sosa
    ? Ken Griff
  tree: |
    +STR
     +DOC ---
      +MAP <tag:yaml.org,2002:set>
       =VAL :Mark McGwire
       =VAL :
       =VAL :Sammy Sosa
       =VAL :
       =VAL :Ken Griff
       =VAL :
      -MAP
     -DOC
    -STR
  json: |
    {
      "Mark McGwire": null,
      "Sammy Sosa": null,
      "Ken Griff": null
    }
  dump: |
    --- !!set
    Mark McGwire:
    Sammy Sosa:
    Ken Griff:
)RAW";

static constexpr std::string_view T_33X3 = R"RAW(
---
- name: Three explicit integers in a block sequence
  from: IRC
  tags: sequence tag
  yaml: |
    ---
    - !!int 1
    - !!int -2
    - !!int 33
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL <tag:yaml.org,2002:int> :1
       =VAL <tag:yaml.org,2002:int> :-2
       =VAL <tag:yaml.org,2002:int> :33
      -SEQ
     -DOC
    -STR
  json: |
    [
      1,
      -2,
      33
    ]
  dump: |
    ---
    - !!int 1
    - !!int -2
    - !!int 33
)RAW";

static constexpr std::string_view T_35KP = R"RAW(
---
- name: Tags for Root Objects
  from: NimYAML tests
  tags: explicit-key header mapping tag
  yaml: |
    --- !!map
    ? a
    : b
    --- !!seq
    - !!str c
    --- !!str
    d
    e
  tree: |
    +STR
     +DOC ---
      +MAP <tag:yaml.org,2002:map>
       =VAL :a
       =VAL :b
      -MAP
     -DOC
     +DOC ---
      +SEQ <tag:yaml.org,2002:seq>
       =VAL <tag:yaml.org,2002:str> :c
      -SEQ
     -DOC
     +DOC ---
      =VAL <tag:yaml.org,2002:str> :d e
     -DOC
    -STR
  json: |
    {
      "a": "b"
    }
    [
      "c"
    ]
    "d e"
  dump: |
    --- !!map
    a: b
    --- !!seq
    - !!str c
    --- !!str d e
)RAW";

static constexpr std::string_view T_36F6 = R"RAW(
---
- name: Multiline plain scalar with empty line
  from: '@perlpunk'
  tags: mapping scalar
  yaml: |
    ---
    plain: a
     b

     c
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :plain
       =VAL :a b\nc
      -MAP
     -DOC
    -STR
  json: |
    {
      "plain": "a b\nc"
    }
  dump: |
    ---
    plain: 'a b

      c'
)RAW";

static constexpr std::string_view T_3ALJ = R"RAW(
---
- name: Block Sequence in Block Sequence
  from: NimYAML tests
  tags: sequence
  yaml: |
    - - s1_i1
      - s1_i2
    - s2
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ
        =VAL :s1_i1
        =VAL :s1_i2
       -SEQ
       =VAL :s2
      -SEQ
     -DOC
    -STR
  json: |
    [
      [
        "s1_i1",
        "s1_i2"
      ],
      "s2"
    ]
)RAW";

static constexpr std::string_view T_3GZX = R"RAW(
---
- name: Spec Example 7.1. Alias Nodes
  from: http://www.yaml.org/spec/1.2/spec.html#id2786448
  tags: mapping spec alias
  yaml: |
    First occurrence: &anchor Foo
    Second occurrence: *anchor
    Override anchor: &anchor Bar
    Reuse anchor: *anchor
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :First occurrence
       =VAL &anchor :Foo
       =VAL :Second occurrence
       =ALI *anchor
       =VAL :Override anchor
       =VAL &anchor :Bar
       =VAL :Reuse anchor
       =ALI *anchor
      -MAP
     -DOC
    -STR
  json: |
    {
      "First occurrence": "Foo",
      "Second occurrence": "Foo",
      "Override anchor": "Bar",
      "Reuse anchor": "Bar"
    }
)RAW";

static constexpr std::string_view T_3HFZ = R"RAW(
---
- name: Invalid content after document end marker
  from: '@perlpunk'
  tags: error footer
  fail: true
  yaml: |
    ---
    key: value
    ... invalid
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key
       =VAL :value
      -MAP
     -DOC ...
)RAW";

static constexpr std::string_view T_3MYT = R"RAW(
---
- name: Plain Scalar looking like key, comment, anchor and tag
  from: https://gist.github.com/anonymous/a98d50ce42a59b1e999552bea7a31f57 via @ingydotnet
  tags: scalar
  yaml: |
    ---
    k:#foo
     &a !t s
  tree: |
    +STR
     +DOC ---
      =VAL :k:#foo &a !t s
     -DOC
    -STR
  json: |
    "k:#foo &a !t s"
  dump: |
    --- k:#foo &a !t s
)RAW";

static constexpr std::string_view T_3R3P = R"RAW(
---
- name: Single block sequence with anchor
  from: '@perlpunk'
  tags: anchor sequence
  yaml: |
    &sequence
    - a
  tree: |
    +STR
     +DOC
      +SEQ &sequence
       =VAL :a
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a"
    ]
  dump: |
    &sequence
    - a
)RAW";

static constexpr std::string_view T_3RLN = R"RAW(
---
- name: Leading tabs in double quoted
  from: '@ingydotnet'
  tags: double whitespace
  yaml: |
    "1 leading
        \ttab"
  tree: |
    +STR
     +DOC
      =VAL "1 leading \ttab
     -DOC
    -STR
  json: |
    "1 leading \ttab"
  emit: |
    "1 leading \ttab"

- yaml: |
    "2 leading
        \———»tab"
  tree: |
    +STR
     +DOC
      =VAL "2 leading \ttab
     -DOC
    -STR
  json: |
    "2 leading \ttab"
  emit: |
    "2 leading \ttab"

- yaml: |
    "3 leading
        ————»tab"
  tree: |
    +STR
     +DOC
      =VAL "3 leading tab
     -DOC
    -STR
  json: |
    "3 leading tab"
  emit: |
    "3 leading tab"

- yaml: |
    "4 leading
        \t  tab"
  tree: |
    +STR
     +DOC
      =VAL "4 leading \t  tab
     -DOC
    -STR
  json: |
    "4 leading \t  tab"
  emit: |
    "4 leading \t  tab"

- yaml: |
    "5 leading
        \———»  tab"
  tree: |
    +STR
     +DOC
      =VAL "5 leading \t  tab
     -DOC
    -STR
  json: |
    "5 leading \t  tab"
  emit: |
    "5 leading \t  tab"

- yaml: |
    "6 leading
        ————»  tab"
  tree: |
    +STR
     +DOC
      =VAL "6 leading tab
     -DOC
    -STR
  json: |
    "6 leading tab"
  emit: |
    "6 leading tab"
)RAW";

static constexpr std::string_view T_3UYS = R"RAW(
---
- name: Escaped slash in double quotes
  from: '@perlpunk'
  tags: double
  yaml: |
    escaped slash: "a\/b"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :escaped slash
       =VAL "a/b
      -MAP
     -DOC
    -STR
  json: |
    {
      "escaped slash": "a/b"
    }
  dump: |
    escaped slash: "a/b"
)RAW";

static constexpr std::string_view T_4ABK = R"RAW(
---
- name: Flow Mapping Separate Values
  from: http://www.yaml.org/spec/1.2/spec.html#id2791704
  tags: flow mapping
  yaml: |
    {
    unquoted : "separate",
    http://foo.com,
    omitted value:,
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :unquoted
       =VAL "separate
       =VAL :http://foo.com
       =VAL :
       =VAL :omitted value
       =VAL :
      -MAP
     -DOC
    -STR
  dump: |
    unquoted: "separate"
    http://foo.com: null
    omitted value: null
)RAW";

static constexpr std::string_view T_4CQQ = R"RAW(
---
- name: Spec Example 2.18. Multi-line Flow Scalars
  from: http://www.yaml.org/spec/1.2/spec.html#id2761268
  tags: spec scalar
  yaml: |
    plain:
      This unquoted scalar
      spans many lines.

    quoted: "So does this
      quoted scalar.\n"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :plain
       =VAL :This unquoted scalar spans many lines.
       =VAL :quoted
       =VAL "So does this quoted scalar.\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "plain": "This unquoted scalar spans many lines.",
      "quoted": "So does this quoted scalar.\n"
    }
  dump: |
    plain: This unquoted scalar spans many lines.
    quoted: "So does this quoted scalar.\n"
)RAW";

static constexpr std::string_view T_4EJS = R"RAW(
---
- name: Invalid tabs as indendation in a mapping
  from: https://github.com/nodeca/js-yaml/issues/80
  tags: error mapping whitespace
  fail: true
  yaml: |
    ---
    a:
    ———»b:
    ———»———»c: value
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
)RAW";

static constexpr std::string_view T_4FJ6 = R"RAW(
---
- name: Nested implicit complex keys
  from: '@perlpunk'
  tags: complex-key flow mapping sequence
  yaml: |
    ---
    [
      [ a, [ [[b,c]]: d, e]]: 23
    ]
  tree: |
    +STR
     +DOC ---
      +SEQ []
       +MAP {}
        +SEQ []
         =VAL :a
         +SEQ []
          +MAP {}
           +SEQ []
            +SEQ []
             =VAL :b
             =VAL :c
            -SEQ
           -SEQ
           =VAL :d
          -MAP
          =VAL :e
         -SEQ
        -SEQ
        =VAL :23
       -MAP
      -SEQ
     -DOC
    -STR
  dump: |
    ---
    - ? - a
        - - ? - - b
                - c
            : d
          - e
      : 23
)RAW";

static constexpr std::string_view T_4GC6 = R"RAW(
---
- name: Spec Example 7.7. Single Quoted Characters
  from: http://www.yaml.org/spec/1.2/spec.html#id2788307
  tags: spec scalar 1.3-err
  yaml: |
    'here''s to "quotes"'
  tree: |
    +STR
     +DOC
      =VAL 'here's to "quotes"
     -DOC
    -STR
  json: |
    "here's to \"quotes\""
)RAW";

static constexpr std::string_view T_4H7K = R"RAW(
---
- name: Flow sequence with invalid extra closing bracket
  from: '@perlpunk'
  tags: error flow sequence
  fail: true
  yaml: |
    ---
    [ a, b, c ] ]
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL :a
       =VAL :b
       =VAL :c
      -SEQ
     -DOC
)RAW";

static constexpr std::string_view T_4HVU = R"RAW(
---
- name: Wrong indendation in Sequence
  from: '@perlpunk'
  tags: error sequence indent
  fail: true
  yaml: |
    key:
       - ok
       - also ok
      - wrong
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +SEQ
        =VAL :ok
        =VAL :also ok
       -SEQ
)RAW";

static constexpr std::string_view T_4JVG = R"RAW(
---
- name: Scalar value with two anchors
  from: '@perlpunk'
  tags: anchor error mapping
  fail: true
  yaml: |
    top1: &node1
      &k1 key1: val1
    top2: &node2
      &v2 val2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :top1
       +MAP &node1
        =VAL &k1 :key1
        =VAL :val1
       -MAP
       =VAL :top2
)RAW";

static constexpr std::string_view T_4MUZ = R"RAW(
---
- name: Flow mapping colon on line after key
  from: '@ingydotnet'
  tags: flow mapping
  yaml: |
    {"foo"
    : "bar"}
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL "foo
       =VAL "bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "bar"
    }
  emit: |
    "foo": "bar"

- yaml: |
    {"foo"
    : bar}
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL "foo
       =VAL :bar
      -MAP
     -DOC
    -STR
  emit: |
    "foo": bar

- yaml: |
    {foo
    : bar}
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :foo
       =VAL :bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "bar"
    }
  emit: |
    foo: bar
)RAW";

static constexpr std::string_view T_4Q9F = R"RAW(
---
- name: Folded Block Scalar [1.3]
  from: TS54, modified for YAML 1.3
  tags: folded scalar 1.3-mod whitespace
  yaml: |
    --- >
     ab
     cd
    ␣
     ef


     gh
  tree: |
    +STR
     +DOC ---
      =VAL >ab cd\nef\n\ngh\n
     -DOC
    -STR
  json: |
    "ab cd\nef\n\ngh\n"
  dump: |
    --- >
      ab cd

      ef


      gh
)RAW";

static constexpr std::string_view T_4QFQ = R"RAW(
---
- name: Spec Example 8.2. Block Indentation Indicator [1.3]
  from: R4YG, modified for YAML 1.3
  tags: spec literal folded scalar libyaml-err 1.3-mod whitespace
  yaml: |
    - |
     detected
    - >
    ␣
    ␣␣
      # detected
    - |1
      explicit
    - >
     detected
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |detected\n
       =VAL >\n\n# detected\n
       =VAL | explicit\n
       =VAL >detected\n
      -SEQ
     -DOC
    -STR
  json: |
    [
      "detected\n",
      "\n\n# detected\n",
      " explicit\n",
      "detected\n"
    ]
  emit: |
    - |
      detected
    - >2


      # detected
    - |2
       explicit
    - >
      detected
)RAW";

static constexpr std::string_view T_4RWC = R"RAW(
---
- name: Trailing spaces after flow collection
  tags: flow whitespace
  from: '@ingydotnet'
  yaml: |2
      [1, 2, 3]␣␣
    ␣␣∎
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL :1
       =VAL :2
       =VAL :3
      -SEQ
     -DOC
    -STR
  json: |
    [
      1,
      2,
      3
    ]
  dump: |
    - 1
    - 2
    - 3
)RAW";

static constexpr std::string_view T_4UYU = R"RAW(
---
- name: Colon in Double Quoted String
  from: NimYAML tests
  tags: mapping scalar 1.3-err
  yaml: |
    "foo: bar\": baz"
  tree: |
    +STR
     +DOC
      =VAL "foo: bar": baz
     -DOC
    -STR
  json: |
    "foo: bar\": baz"
)RAW";

static constexpr std::string_view T_4V8U = R"RAW(
---
- name: Plain scalar with backslashes
  from: '@perlpunk'
  tags: scalar
  yaml: |
    ---
    plain\value\with\backslashes
  tree: |
    +STR
     +DOC ---
      =VAL :plain\\value\\with\\backslashes
     -DOC
    -STR
  json: |
    "plain\\value\\with\\backslashes"
  dump: |
    --- plain\value\with\backslashes
)RAW";

static constexpr std::string_view T_4WA9 = R"RAW(
---
- name: Literal scalars
  from: '@ingydotnet'
  tags: indent literal
  yaml: |
    - aaa: |2
        xxx
      bbb: |
        xxx
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :aaa
        =VAL |xxx\n
        =VAL :bbb
        =VAL |xxx\n
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "aaa" : "xxx\n",
        "bbb" : "xxx\n"
      }
    ]
  dump: |
    ---
    - aaa: |
        xxx
      bbb: |
        xxx
  emit: |
    - aaa: |
        xxx
      bbb: |
        xxx
)RAW";

static constexpr std::string_view T_4ZYM = R"RAW(
---
- name: Spec Example 6.4. Line Prefixes
  from: http://www.yaml.org/spec/1.2/spec.html#id2778720
  tags: spec scalar literal double upto-1.2 whitespace
  yaml: |
    plain: text
      lines
    quoted: "text
      —»lines"
    block: |
      text
       »lines
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :plain
       =VAL :text lines
       =VAL :quoted
       =VAL "text lines
       =VAL :block
       =VAL |text\n \tlines\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "plain": "text lines",
      "quoted": "text lines",
      "block": "text\n \tlines\n"
    }
  dump: |
    plain: text lines
    quoted: "text lines"
    block: "text\n \tlines\n"
  emit: |
    plain: text lines
    quoted: "text lines"
    block: |
      text
       »lines
)RAW";

static constexpr std::string_view T_52DL = R"RAW(
---
- name: Explicit Non-Specific Tag [1.3]
  from: 8MK2, modified for YAML 1.3
  tags: tag 1.3-mod
  yaml: |
    ---
    ! a
  tree: |
    +STR
     +DOC ---
      =VAL <!> :a
     -DOC
    -STR
  json: |
    "a"
  dump: |
    --- ! a
)RAW";

static constexpr std::string_view T_54T7 = R"RAW(
---
- name: Flow Mapping
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/mapping.tml
  tags: flow mapping
  yaml: |
    {foo: you, bar: far}
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :foo
       =VAL :you
       =VAL :bar
       =VAL :far
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "you",
      "bar": "far"
    }
  dump: |
    foo: you
    bar: far
)RAW";

static constexpr std::string_view T_55WF = R"RAW(
---
- name: Invalid escape in double quoted string
  from: '@perlpunk'
  tags: error double
  fail: true
  yaml: |
    ---
    "\."
  tree: |
    +STR
     +DOC ---
)RAW";

static constexpr std::string_view T_565N = R"RAW(
---
- name: Construct Binary
  from: https://github.com/yaml/pyyaml/blob/master/tests/data/construct-binary-py2.data
  tags: tag unknown-tag
  yaml: |
    canonical: !!binary "\
     R0lGODlhDAAMAIQAAP//9/X17unp5WZmZgAAAOfn515eXvPz7Y6OjuDg4J+fn5\
     OTk6enp56enmlpaWNjY6Ojo4SEhP/++f/++f/++f/++f/++f/++f/++f/++f/+\
     +f/++f/++f/++f/++f/++SH+Dk1hZGUgd2l0aCBHSU1QACwAAAAADAAMAAAFLC\
     AgjoEwnuNAFOhpEMTRiggcz4BNJHrv/zCFcLiwMWYNG84BwwEeECcgggoBADs="
    generic: !!binary |
     R0lGODlhDAAMAIQAAP//9/X17unp5WZmZgAAAOfn515eXvPz7Y6OjuDg4J+fn5
     OTk6enp56enmlpaWNjY6Ojo4SEhP/++f/++f/++f/++f/++f/++f/++f/++f/+
     +f/++f/++f/++f/++f/++SH+Dk1hZGUgd2l0aCBHSU1QACwAAAAADAAMAAAFLC
     AgjoEwnuNAFOhpEMTRiggcz4BNJHrv/zCFcLiwMWYNG84BwwEeECcgggoBADs=
    description:
     The binary value above is a tiny arrow encoded as a gif image.
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :canonical
       =VAL <tag:yaml.org,2002:binary> "R0lGODlhDAAMAIQAAP//9/X17unp5WZmZgAAAOfn515eXvPz7Y6OjuDg4J+fn5OTk6enp56enmlpaWNjY6Ojo4SEhP/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++SH+Dk1hZGUgd2l0aCBHSU1QACwAAAAADAAMAAAFLCAgjoEwnuNAFOhpEMTRiggcz4BNJHrv/zCFcLiwMWYNG84BwwEeECcgggoBADs=
       =VAL :generic
       =VAL <tag:yaml.org,2002:binary> |R0lGODlhDAAMAIQAAP//9/X17unp5WZmZgAAAOfn515eXvPz7Y6OjuDg4J+fn5\nOTk6enp56enmlpaWNjY6Ojo4SEhP/++f/++f/++f/++f/++f/++f/++f/++f/+\n+f/++f/++f/++f/++f/++SH+Dk1hZGUgd2l0aCBHSU1QACwAAAAADAAMAAAFLC\nAgjoEwnuNAFOhpEMTRiggcz4BNJHrv/zCFcLiwMWYNG84BwwEeECcgggoBADs=\n
       =VAL :description
       =VAL :The binary value above is a tiny arrow encoded as a gif image.
      -MAP
     -DOC
    -STR
  json: |
    {
      "canonical": "R0lGODlhDAAMAIQAAP//9/X17unp5WZmZgAAAOfn515eXvPz7Y6OjuDg4J+fn5OTk6enp56enmlpaWNjY6Ojo4SEhP/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++f/++SH+Dk1hZGUgd2l0aCBHSU1QACwAAAAADAAMAAAFLCAgjoEwnuNAFOhpEMTRiggcz4BNJHrv/zCFcLiwMWYNG84BwwEeECcgggoBADs=",
      "generic": "R0lGODlhDAAMAIQAAP//9/X17unp5WZmZgAAAOfn515eXvPz7Y6OjuDg4J+fn5\nOTk6enp56enmlpaWNjY6Ojo4SEhP/++f/++f/++f/++f/++f/++f/++f/++f/+\n+f/++f/++f/++f/++f/++SH+Dk1hZGUgd2l0aCBHSU1QACwAAAAADAAMAAAFLC\nAgjoEwnuNAFOhpEMTRiggcz4BNJHrv/zCFcLiwMWYNG84BwwEeECcgggoBADs=\n",
      "description": "The binary value above is a tiny arrow encoded as a gif image."
    }
)RAW";

static constexpr std::string_view T_57H4 = R"RAW(
---
- name: Spec Example 8.22. Block Collection Nodes
  from: http://www.yaml.org/spec/1.2/spec.html#id2800008
  tags: sequence mapping tag
  yaml: |
    sequence: !!seq
    - entry
    - !!seq
     - nested
    mapping: !!map
     foo: bar
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :sequence
       +SEQ <tag:yaml.org,2002:seq>
        =VAL :entry
        +SEQ <tag:yaml.org,2002:seq>
         =VAL :nested
        -SEQ
       -SEQ
       =VAL :mapping
       +MAP <tag:yaml.org,2002:map>
        =VAL :foo
        =VAL :bar
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "sequence": [
        "entry",
        [
          "nested"
        ]
      ],
      "mapping": {
        "foo": "bar"
      }
    }
  dump: |
    sequence: !!seq
    - entry
    - !!seq
      - nested
    mapping: !!map
      foo: bar
)RAW";

static constexpr std::string_view T_58MP = R"RAW(
---
- name: Flow mapping edge cases
  from: '@ingydotnet'
  tags: edge flow mapping
  yaml: |
    {x: :x}
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :x
       =VAL ::x
      -MAP
     -DOC
    -STR
  json: |
    {
      "x": ":x"
    }
  dump: |
    x: :x
)RAW";

static constexpr std::string_view T_5BVJ = R"RAW(
---
- name: Spec Example 5.7. Block Scalar Indicators
  from: http://www.yaml.org/spec/1.2/spec.html#id2773653
  tags: spec literal folded scalar
  yaml: |
    literal: |
      some
      text
    folded: >
      some
      text
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :literal
       =VAL |some\ntext\n
       =VAL :folded
       =VAL >some text\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "literal": "some\ntext\n",
      "folded": "some text\n"
    }
  dump: |
    literal: |
      some
      text
    folded: >
      some text
)RAW";

static constexpr std::string_view T_5C5M = R"RAW(
---
- name: Spec Example 7.15. Flow Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2791018
  tags: spec flow mapping
  yaml: |
    - { one : two , three: four , }
    - {five: six,seven : eight}
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP {}
        =VAL :one
        =VAL :two
        =VAL :three
        =VAL :four
       -MAP
       +MAP {}
        =VAL :five
        =VAL :six
        =VAL :seven
        =VAL :eight
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "one": "two",
        "three": "four"
      },
      {
        "five": "six",
        "seven": "eight"
      }
    ]
  dump: |
    - one: two
      three: four
    - five: six
      seven: eight
)RAW";

static constexpr std::string_view T_5GBF = R"RAW(
---
- name: Spec Example 6.5. Empty Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2778971
  tags: double literal spec scalar upto-1.2 whitespace
  yaml: |
    Folding:
      "Empty line
       »
      as a line feed"
    Chomping: |
      Clipped empty lines
    ␣
    ↵
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :Folding
       =VAL "Empty line\nas a line feed
       =VAL :Chomping
       =VAL |Clipped empty lines\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "Folding": "Empty line\nas a line feed",
      "Chomping": "Clipped empty lines\n"
    }
  dump: |
    Folding: "Empty line\nas a line feed"
    Chomping: |
      Clipped empty lines
)RAW";

static constexpr std::string_view T_5KJE = R"RAW(
---
- name: Spec Example 7.13. Flow Sequence
  from: http://www.yaml.org/spec/1.2/spec.html#id2790506
  tags: spec flow sequence
  yaml: |
    - [ one, two, ]
    - [three ,four]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        =VAL :one
        =VAL :two
       -SEQ
       +SEQ []
        =VAL :three
        =VAL :four
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      [
        "one",
        "two"
      ],
      [
        "three",
        "four"
      ]
    ]
  dump: |
    - - one
      - two
    - - three
      - four
)RAW";

static constexpr std::string_view T_5LLU = R"RAW(
---
- name: Block scalar with wrong indented line after spaces only
  from: '@perlpunk'
  tags: error folded whitespace
  fail: true
  yaml: |
    block scalar: >
    ␣
    ␣␣
    ␣␣␣
     invalid
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :block scalar
)RAW";

static constexpr std::string_view T_5MUD = R"RAW(
---
- name: Colon and adjacent value on next line
  from: '@perlpunk'
  tags: double flow mapping
  yaml: |
    ---
    { "foo"
      :bar }
  tree: |
    +STR
     +DOC ---
      +MAP {}
       =VAL "foo
       =VAL :bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "bar"
    }
  dump: |
    ---
    "foo": bar
)RAW";

static constexpr std::string_view T_5NYZ = R"RAW(
---
- name: Spec Example 6.9. Separated Comment
  from: http://www.yaml.org/spec/1.2/spec.html#id2780342
  tags: mapping spec comment
  yaml: |
    key:    # Comment
      value
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL :value
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": "value"
    }
  dump: |
    key: value
)RAW";

static constexpr std::string_view T_5T43 = R"RAW(
---
- name: Colon at the beginning of adjacent flow scalar
  from: '@perlpunk'
  tags: flow mapping scalar
  yaml: |
    - { "key":value }
    - { "key"::value }
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP {}
        =VAL "key
        =VAL :value
       -MAP
       +MAP {}
        =VAL "key
        =VAL ::value
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "key": "value"
      },
      {
        "key": ":value"
      }
    ]
  dump: |
    - key: value
    - key: :value
  emit: |
    - "key": value
    - "key": :value
)RAW";

static constexpr std::string_view T_5TRB = R"RAW(
---
- name: Invalid document-start marker in doublequoted tring
  from: '@perlpunk'
  tags: header double error
  fail: true
  yaml: |
    ---
    "
    ---
    "
  tree: |
    +STR
     +DOC ---
)RAW";

static constexpr std::string_view T_5TYM = R"RAW(
---
- name: Spec Example 6.21. Local Tag Prefix
  from: http://www.yaml.org/spec/1.2/spec.html#id2783499
  tags: local-tag spec directive tag
  yaml: |
    %TAG !m! !my-
    --- # Bulb here
    !m!light fluorescent
    ...
    %TAG !m! !my-
    --- # Color here
    !m!light green
  tree: |
    +STR
     +DOC ---
      =VAL <!my-light> :fluorescent
     -DOC ...
     +DOC ---
      =VAL <!my-light> :green
     -DOC
    -STR
  json: |
    "fluorescent"
    "green"
)RAW";

static constexpr std::string_view T_5U3A = R"RAW(
---
- name: Sequence on same Line as Mapping Key
  from: '@perlpunk'
  tags: error sequence mapping
  fail: true
  yaml: |
    key: - a
         - b
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
)RAW";

static constexpr std::string_view T_5WE3 = R"RAW(
---
- name: Spec Example 8.17. Explicit Block Mapping Entries
  from: http://www.yaml.org/spec/1.2/spec.html#id2798425
  tags: explicit-key spec mapping comment literal sequence
  yaml: |
    ? explicit key # Empty value
    ? |
      block key
    : - one # Explicit compact
      - two # block value
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :explicit key
       =VAL :
       =VAL |block key\n
       +SEQ
        =VAL :one
        =VAL :two
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "explicit key": null,
      "block key\n": [
        "one",
        "two"
      ]
    }
  dump: |
    explicit key:
    ? |
      block key
    : - one
      - two
)RAW";

static constexpr std::string_view T_62EZ = R"RAW(
---
- name: Invalid block mapping key on same line as previous key
  from: '@perlpunk'
  tags: error flow mapping
  fail: true
  yaml: |
    ---
    x: { y: z }in: valid
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :x
       +MAP {}
        =VAL :y
        =VAL :z
       -MAP
)RAW";

static constexpr std::string_view T_652Z = R"RAW(
---
- name: Question mark at start of flow key
  from: '@ingydotnet'
  tags: flow
  yaml: |
    { ?foo: bar,
    bar: 42
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :?foo
       =VAL :bar
       =VAL :bar
       =VAL :42
      -MAP
     -DOC
    -STR
  json: |
    {
      "?foo" : "bar",
      "bar" : 42
    }
  dump: |
    ---
    ?foo: bar
    bar: 42
  emit: |
    ?foo: bar
    bar: 42
)RAW";

static constexpr std::string_view T_65WH = R"RAW(
---
- name: Single Entry Block Sequence
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/sequence.tml
  tags: sequence
  yaml: |
    - foo
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :foo
      -SEQ
     -DOC
    -STR
  json: |
    [
      "foo"
    ]
)RAW";

static constexpr std::string_view T_6BCT = R"RAW(
---
- name: Spec Example 6.3. Separation Spaces
  from: http://www.yaml.org/spec/1.2/spec.html#id2778394
  tags: spec libyaml-err sequence whitespace upto-1.2
  yaml: |
    - foo:—» bar
    - - baz
      -»baz
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :foo
        =VAL :bar
       -MAP
       +SEQ
        =VAL :baz
        =VAL :baz
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "foo": "bar"
      },
      [
        "baz",
        "baz"
      ]
    ]
  dump: |
    - foo: bar
    - - baz
      - baz
)RAW";

static constexpr std::string_view T_6BFJ = R"RAW(
---
- name: Mapping, key and flow sequence item anchors
  from: '@perlpunk'
  tags: anchor complex-key flow mapping sequence
  yaml: |
    ---
    &mapping
    &key [ &item a, b, c ]: value
  tree: |
    +STR
     +DOC ---
      +MAP &mapping
       +SEQ [] &key
        =VAL &item :a
        =VAL :b
        =VAL :c
       -SEQ
       =VAL :value
      -MAP
     -DOC
    -STR
  dump: |
    --- &mapping
    ? &key
    - &item a
    - b
    - c
    : value
)RAW";

static constexpr std::string_view T_6CA3 = R"RAW(
---
- name: Tab indented top flow
  from: '@ingydotnet'
  tags: indent whitespace
  yaml: |
    ————»[
    ————»]
  tree: |
    +STR
     +DOC
      +SEQ []
      -SEQ
     -DOC
    -STR
  json: |
    []
  emit: |
    --- []
)RAW";

static constexpr std::string_view T_6CK3 = R"RAW(
---
- name: Spec Example 6.26. Tag Shorthands
  from: http://www.yaml.org/spec/1.2/spec.html#id2785009
  tags: spec tag local-tag
  yaml: |
    %TAG !e! tag:example.com,2000:app/
    ---
    - !local foo
    - !!str bar
    - !e!tag%21 baz
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL <!local> :foo
       =VAL <tag:yaml.org,2002:str> :bar
       =VAL <tag:example.com,2000:app/tag!> :baz
      -SEQ
     -DOC
    -STR
  json: |
    [
      "foo",
      "bar",
      "baz"
    ]
)RAW";

static constexpr std::string_view T_6FWR = R"RAW(
---
- name: Block Scalar Keep
  from: NimYAML tests
  tags: literal scalar whitespace
  yaml: |
    --- |+
     ab
    ␣
    ␣␣
    ...
  tree: |
    +STR
     +DOC ---
      =VAL |ab\n\n \n
     -DOC ...
    -STR
  json: |
    "ab\n\n \n"
  dump: |
    "ab\n\n \n"
    ...
  emit: |
    --- |
      ab

    ␣␣␣
    ...
)RAW";

static constexpr std::string_view T_6H3V = R"RAW(
---
- name: Backslashes in singlequotes
  from: '@perlpunk'
  tags: scalar single
  yaml: |
    'foo: bar\': baz'
  tree: |
    +STR
     +DOC
      +MAP
       =VAL 'foo: bar\\
       =VAL :baz'
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo: bar\\": "baz'"
    }
  dump: |
    'foo: bar\': baz'
)RAW";

static constexpr std::string_view T_6HB6 = R"RAW(
---
- name: Spec Example 6.1. Indentation Spaces
  from: http://www.yaml.org/spec/1.2/spec.html#id2777865
  tags: comment flow spec indent upto-1.2 whitespace
  yaml: |2
      # Leading comment line spaces are
       # neither content nor indentation.
    ␣␣␣␣
    Not indented:
     By one space: |
        By four
          spaces
     Flow style: [    # Leading spaces
       By two,        # in flow style
      Also by two,    # are neither
      —»Still by two   # content nor
        ]             # indentation.
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :Not indented
       +MAP
        =VAL :By one space
        =VAL |By four\n  spaces\n
        =VAL :Flow style
        +SEQ []
         =VAL :By two
         =VAL :Also by two
         =VAL :Still by two
        -SEQ
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "Not indented": {
        "By one space": "By four\n  spaces\n",
        "Flow style": [
          "By two",
          "Also by two",
          "Still by two"
        ]
      }
    }
  dump: |
    Not indented:
      By one space: |
        By four
          spaces
      Flow style:
      - By two
      - Also by two
      - Still by two
)RAW";

static constexpr std::string_view T_6JQW = R"RAW(
---
- name: Spec Example 2.13. In literals, newlines are preserved
  from: http://www.yaml.org/spec/1.2/spec.html#id2759963
  tags: spec scalar literal comment
  yaml: |
    # ASCII Art
    --- |
      \//||\/||
      // ||  ||__
  tree: |
    +STR
     +DOC ---
      =VAL |\\//||\\/||\n// ||  ||__\n
     -DOC
    -STR
  json: |
    "\\//||\\/||\n// ||  ||__\n"
  dump: |
    --- |
      \//||\/||
      // ||  ||__
)RAW";

static constexpr std::string_view T_6JTT = R"RAW(
---
- name: Flow sequence without closing bracket
  from: '@perlpunk'
  tags: error flow sequence
  fail: true
  yaml: |
    ---
    [ [ a, b, c ]
  tree: |
    +STR
     +DOC ---
      +SEQ []
       +SEQ []
        =VAL :a
        =VAL :b
        =VAL :c
       -SEQ
)RAW";

static constexpr std::string_view T_6JWB = R"RAW(
---
- name: Tags for Block Objects
  from: NimYAML tests
  tags: mapping sequence tag
  yaml: |
    foo: !!seq
      - !!str a
      - !!map
        key: !!str value
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       +SEQ <tag:yaml.org,2002:seq>
        =VAL <tag:yaml.org,2002:str> :a
        +MAP <tag:yaml.org,2002:map>
         =VAL :key
         =VAL <tag:yaml.org,2002:str> :value
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": [
        "a",
        {
          "key": "value"
        }
      ]
    }
  dump: |
    foo: !!seq
    - !!str a
    - !!map
      key: !!str value
)RAW";

static constexpr std::string_view T_6KGN = R"RAW(
---
- name: Anchor for empty node
  from: https://github.com/nodeca/js-yaml/issues/301
  tags: alias anchor
  yaml: |
    ---
    a: &anchor
    b: *anchor
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
       =VAL &anchor :
       =VAL :b
       =ALI *anchor
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": null,
      "b": null
    }
  dump: |
    ---
    a: &anchor
    b: *anchor
)RAW";

static constexpr std::string_view T_6LVF = R"RAW(
---
- name: Spec Example 6.13. Reserved Directives
  from: http://www.yaml.org/spec/1.2/spec.html#id2781445
  tags: spec directive header double 1.3-err
  yaml: |
    %FOO  bar baz # Should be ignored
                  # with a warning.
    --- "foo"
  tree: |
    +STR
     +DOC ---
      =VAL "foo
     -DOC
    -STR
  json: |
    "foo"
  dump: |
    --- "foo"
)RAW";

static constexpr std::string_view T_6M2F = R"RAW(
---
- name: Aliases in Explicit Block Mapping
  from: NimYAML tests
  tags: alias explicit-key empty-key
  yaml: |
    ? &a a
    : &b b
    : *a
  tree: |
    +STR
     +DOC
      +MAP
       =VAL &a :a
       =VAL &b :b
       =VAL :
       =ALI *a
      -MAP
     -DOC
    -STR
  dump: |
    &a a: &b b
    : *a
)RAW";

static constexpr std::string_view T_6PBE = R"RAW(
---
- name: Zero-indented sequences in explicit mapping keys
  from: '@perlpunk'
  tags: explicit-key mapping sequence
  yaml: |
    ---
    ?
    - a
    - b
    :
    - c
    - d
  tree: |
    +STR
     +DOC ---
      +MAP
       +SEQ
        =VAL :a
        =VAL :b
       -SEQ
       +SEQ
        =VAL :c
        =VAL :d
       -SEQ
      -MAP
     -DOC
    -STR
  emit: |
    ---
    ? - a
      - b
    : - c
      - d
)RAW";

static constexpr std::string_view T_6S55 = R"RAW(
---
- name: Invalid scalar at the end of sequence
  from: '@perlpunk'
  tags: error mapping sequence
  fail: true
  yaml: |
    key:
     - bar
     - baz
     invalid
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +SEQ
        =VAL :bar
        =VAL :baz
)RAW";

static constexpr std::string_view T_6SLA = R"RAW(
---
- name: Allowed characters in quoted mapping key
  from: '@perlpunk'
  tags: mapping single double
  yaml: |
    "foo\nbar:baz\tx \\$%^&*()x": 23
    'x\ny:z\tx $%^&*()x': 24
  tree: |
    +STR
     +DOC
      +MAP
       =VAL "foo\nbar:baz\tx \\$%^&*()x
       =VAL :23
       =VAL 'x\\ny:z\\tx $%^&*()x
       =VAL :24
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo\nbar:baz\tx \\$%^&*()x": 23,
      "x\\ny:z\\tx $%^&*()x": 24
    }
  dump: |
    ? "foo\nbar:baz\tx \\$%^&*()x"
    : 23
    'x\ny:z\tx $%^&*()x': 24
)RAW";

static constexpr std::string_view T_6VJK = R"RAW(
---
- name: Spec Example 2.15. Folded newlines are preserved for "more indented" and blank lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2761056
  tags: spec folded scalar 1.3-err
  yaml: |
    >
     Sammy Sosa completed another
     fine season with great stats.

       63 Home Runs
       0.288 Batting Average

     What a year!
  tree: |
    +STR
     +DOC
      =VAL >Sammy Sosa completed another fine season with great stats.\n\n  63 Home Runs\n  0.288 Batting Average\n\nWhat a year!\n
     -DOC
    -STR
  json: |
    "Sammy Sosa completed another fine season with great stats.\n\n  63 Home Runs\n  0.288 Batting Average\n\nWhat a year!\n"
  dump: |
    >
      Sammy Sosa completed another fine season with great stats.

        63 Home Runs
        0.288 Batting Average

      What a year!
)RAW";

static constexpr std::string_view T_6WLZ = R"RAW(
---
- name: Spec Example 6.18. Primary Tag Handle [1.3]
  from: 9WXW, modified for YAML 1.3
  tags: local-tag spec directive tag 1.3-mod
  yaml: |
    # Private
    ---
    !foo "bar"
    ...
    # Global
    %TAG ! tag:example.com,2000:app/
    ---
    !foo "bar"
  tree: |
    +STR
     +DOC ---
      =VAL <!foo> "bar
     -DOC ...
     +DOC ---
      =VAL <tag:example.com,2000:app/foo> "bar
     -DOC
    -STR
  json: |
    "bar"
    "bar"
  dump: |
    ---
    !foo "bar"
    ...
    --- !<tag:example.com,2000:app/foo>
    "bar"
  emit: |
    --- !foo "bar"
    ...
    --- !<tag:example.com,2000:app/foo> "bar"
)RAW";

static constexpr std::string_view T_6WPF = R"RAW(
---
- name: Spec Example 6.8. Flow Folding [1.3]
  from: TL85, modified for YAML 1.3
  tags: double spec whitespace scalar 1.3-mod
  yaml: |
    ---
    "
      foo␣
    ␣
        bar

      baz
    "
  tree: |
    +STR
     +DOC ---
      =VAL " foo\nbar\nbaz␣
     -DOC
    -STR
  json: |
    " foo\nbar\nbaz "
  dump: |
    " foo\nbar\nbaz "
  emit: |
    --- " foo\nbar\nbaz "
)RAW";

static constexpr std::string_view T_6XDY = R"RAW(
---
- name: Two document start markers
  from: '@perlpunk'
  tags: header
  yaml: |
    ---
    ---
  tree: |
    +STR
     +DOC ---
      =VAL :
     -DOC
     +DOC ---
      =VAL :
     -DOC
    -STR
  json: |
    null
    null
  dump: |
    ---
    ---
)RAW";

static constexpr std::string_view T_6ZKB = R"RAW(
---
- name: Spec Example 9.6. Stream
  from: http://www.yaml.org/spec/1.2/spec.html#id2801896
  tags: spec header 1.3-err
  yaml: |
    Document
    ---
    # Empty
    ...
    %YAML 1.2
    ---
    matches %: 20
  tree: |
    +STR
     +DOC
      =VAL :Document
     -DOC
     +DOC ---
      =VAL :
     -DOC ...
     +DOC ---
      +MAP
       =VAL :matches %
       =VAL :20
      -MAP
     -DOC
    -STR
  json: |
    "Document"
    null
    {
      "matches %": 20
    }
  emit: |
    Document
    ---
    ...
    %YAML 1.2
    ---
    matches %: 20
)RAW";

static constexpr std::string_view T_735Y = R"RAW(
---
- name: Spec Example 8.20. Block Node Types
  from: http://www.yaml.org/spec/1.2/spec.html#id2799426
  tags: comment double spec folded tag
  yaml: |
    -
      "flow in block"
    - >
     Block scalar
    - !!map # Block collection
      foo : bar
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL "flow in block
       =VAL >Block scalar\n
       +MAP <tag:yaml.org,2002:map>
        =VAL :foo
        =VAL :bar
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      "flow in block",
      "Block scalar\n",
      {
        "foo": "bar"
      }
    ]
  dump: |
    - "flow in block"
    - >
      Block scalar
    - !!map
      foo: bar
)RAW";

static constexpr std::string_view T_74H7 = R"RAW(
---
- name: Tags in Implicit Mapping
  from: NimYAML tests
  tags: tag mapping
  yaml: |
    !!str a: b
    c: !!int 42
    e: !!str f
    g: h
    !!str 23: !!bool false
  tree: |
    +STR
     +DOC
      +MAP
       =VAL <tag:yaml.org,2002:str> :a
       =VAL :b
       =VAL :c
       =VAL <tag:yaml.org,2002:int> :42
       =VAL :e
       =VAL <tag:yaml.org,2002:str> :f
       =VAL :g
       =VAL :h
       =VAL <tag:yaml.org,2002:str> :23
       =VAL <tag:yaml.org,2002:bool> :false
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": "b",
      "c": 42,
      "e": "f",
      "g": "h",
      "23": false
    }
  dump: |
    !!str a: b
    c: !!int 42
    e: !!str f
    g: h
    !!str 23: !!bool false
)RAW";

static constexpr std::string_view T_753E = R"RAW(
---
- name: Block Scalar Strip [1.3]
  from: MYW6, modified for YAML 1.3
  tags: literal scalar 1.3-mod whitespace
  yaml: |
    --- |-
     ab
    ␣
    ␣
    ...
  tree: |
    +STR
     +DOC ---
      =VAL |ab
     -DOC ...
    -STR
  json: |
    "ab"
  dump: |
    --- |-
      ab
    ...
)RAW";

static constexpr std::string_view T_7A4E = R"RAW(
---
- name: Spec Example 7.6. Double Quoted Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2787994
  tags: spec scalar upto-1.2 whitespace
  yaml: |
    " 1st non-empty

     2nd non-empty␣
    ———»3rd non-empty "
  tree: |
    +STR
     +DOC
      =VAL " 1st non-empty\n2nd non-empty 3rd non-empty␣
     -DOC
    -STR
  json: |
    " 1st non-empty\n2nd non-empty 3rd non-empty "
  dump: |
    " 1st non-empty\n2nd non-empty 3rd non-empty "
)RAW";

static constexpr std::string_view T_7BMT = R"RAW(
---
- name: Node and Mapping Key Anchors [1.3]
  from: U3XV, modified for YAML 1.3
  tags: anchor comment mapping 1.3-mod
  yaml: |
    ---
    top1: &node1
      &k1 key1: one
    top2: &node2 # comment
      key2: two
    top3:
      &k3 key3: three
    top4: &node4
      &k4 key4: four
    top5: &node5
      key5: five
    top6: &val6
      six
    top7:
      &val7 seven
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :top1
       +MAP &node1
        =VAL &k1 :key1
        =VAL :one
       -MAP
       =VAL :top2
       +MAP &node2
        =VAL :key2
        =VAL :two
       -MAP
       =VAL :top3
       +MAP
        =VAL &k3 :key3
        =VAL :three
       -MAP
       =VAL :top4
       +MAP &node4
        =VAL &k4 :key4
        =VAL :four
       -MAP
       =VAL :top5
       +MAP &node5
        =VAL :key5
        =VAL :five
       -MAP
       =VAL :top6
       =VAL &val6 :six
       =VAL :top7
       =VAL &val7 :seven
      -MAP
     -DOC
    -STR
  json: |
    {
      "top1": {
        "key1": "one"
      },
      "top2": {
        "key2": "two"
      },
      "top3": {
        "key3": "three"
      },
      "top4": {
        "key4": "four"
      },
      "top5": {
        "key5": "five"
      },
      "top6": "six",
      "top7": "seven"
    }
  dump: |
    ---
    top1: &node1
      &k1 key1: one
    top2: &node2
      key2: two
    top3:
      &k3 key3: three
    top4: &node4
      &k4 key4: four
    top5: &node5
      key5: five
    top6: &val6 six
    top7: &val7 seven
)RAW";

static constexpr std::string_view T_7BUB = R"RAW(
---
- name: Spec Example 2.10. Node for “Sammy Sosa” appears twice in this document
  from: http://www.yaml.org/spec/1.2/spec.html#id2760658
  tags: mapping sequence spec alias
  yaml: |
    ---
    hr:
      - Mark McGwire
      # Following node labeled SS
      - &SS Sammy Sosa
    rbi:
      - *SS # Subsequent occurrence
      - Ken Griffey
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :hr
       +SEQ
        =VAL :Mark McGwire
        =VAL &SS :Sammy Sosa
       -SEQ
       =VAL :rbi
       +SEQ
        =ALI *SS
        =VAL :Ken Griffey
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "hr": [
        "Mark McGwire",
        "Sammy Sosa"
      ],
      "rbi": [
        "Sammy Sosa",
        "Ken Griffey"
      ]
    }
  dump: |
    ---
    hr:
    - Mark McGwire
    - &SS Sammy Sosa
    rbi:
    - *SS
    - Ken Griffey
)RAW";

static constexpr std::string_view T_7FWL = R"RAW(
---
- name: Spec Example 6.24. Verbatim Tags
  from: http://www.yaml.org/spec/1.2/spec.html#id2784370
  tags: mapping spec tag unknown-tag
  yaml: |
    !<tag:yaml.org,2002:str> foo :
      !<!bar> baz
  tree: |
    +STR
     +DOC
      +MAP
       =VAL <tag:yaml.org,2002:str> :foo
       =VAL <!bar> :baz
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "baz"
    }
  dump: |
    !!str foo: !bar baz
)RAW";

static constexpr std::string_view T_7LBH = R"RAW(
---
- name: Multiline double quoted implicit keys
  from: '@perlpunk'
  tags: error double
  fail: true
  yaml: |
    "a\nb": 1
    "c
     d": 1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL "a\nb
       =VAL :1
)RAW";

static constexpr std::string_view T_7MNF = R"RAW(
---
- name: Missing colon
  from: '@perlpunk'
  tags: error mapping
  fail: true
  yaml: |
    top1:
      key1: val1
    top2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :top1
       +MAP
        =VAL :key1
        =VAL :val1
       -MAP
)RAW";

static constexpr std::string_view T_7T8X = R"RAW(
---
- name: Spec Example 8.10. Folded Lines - 8.13. Final Empty Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2796543
  tags: spec folded scalar comment 1.3-err
  yaml: |
    >

     folded
     line

     next
     line
       * bullet

       * list
       * lines

     last
     line

    # Comment
  tree: |
    +STR
     +DOC
      =VAL >\nfolded line\nnext line\n  * bullet\n\n  * list\n  * lines\n\nlast line\n
     -DOC
    -STR
  json: |
    "\nfolded line\nnext line\n  * bullet\n\n  * list\n  * lines\n\nlast line\n"
  dump: |
    >

      folded line

      next line
        * bullet

        * list
        * lines

      last line
)RAW";

static constexpr std::string_view T_7TMG = R"RAW(
---
- name: Comment in flow sequence before comma
  from: '@perlpunk'
  tags: comment flow sequence
  yaml: |
    ---
    [ word1
    # comment
    , word2]
  tree: |
    +STR
     +DOC ---
      +SEQ []
       =VAL :word1
       =VAL :word2
      -SEQ
     -DOC
    -STR
  json: |
    [
      "word1",
      "word2"
    ]
  dump: |
    ---
    - word1
    - word2
)RAW";

static constexpr std::string_view T_7W2P = R"RAW(
---
- name: Block Mapping with Missing Values
  from: NimYAML tests
  tags: explicit-key mapping
  yaml: |
    ? a
    ? b
    c:
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL :
       =VAL :b
       =VAL :
       =VAL :c
       =VAL :
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": null,
      "b": null,
      "c": null
    }
  dump: |
    a:
    b:
    c:
)RAW";

static constexpr std::string_view T_7Z25 = R"RAW(
---
- name: Bare document after document end marker
  from: '@perlpunk'
  tags: footer
  yaml: |
    ---
    scalar1
    ...
    key: value
  tree: |
    +STR
     +DOC ---
      =VAL :scalar1
     -DOC ...
     +DOC
      +MAP
       =VAL :key
       =VAL :value
      -MAP
     -DOC
    -STR
  json: |
    "scalar1"
    {
      "key": "value"
    }
  dump: |
    --- scalar1
    ...
    key: value
)RAW";

static constexpr std::string_view T_7ZZ5 = R"RAW(
---
- name: Empty flow collections
  from: '@perlpunk'
  tags: flow mapping sequence
  yaml: |
    ---
    nested sequences:
    - - - []
    - - - {}
    key1: []
    key2: {}
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :nested sequences
       +SEQ
        +SEQ
         +SEQ
          +SEQ []
          -SEQ
         -SEQ
        -SEQ
        +SEQ
         +SEQ
          +MAP {}
          -MAP
         -SEQ
        -SEQ
       -SEQ
       =VAL :key1
       +SEQ []
       -SEQ
       =VAL :key2
       +MAP {}
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "nested sequences": [
        [
          [
            []
          ]
        ],
        [
          [
            {}
          ]
        ]
      ],
      "key1": [],
      "key2": {}
    }
  dump: |
    ---
    nested sequences:
    - - - []
    - - - {}
    key1: []
    key2: {}
)RAW";

static constexpr std::string_view T_82AN = R"RAW(
---
- name: Three dashes and content without space
  from: '@perlpunk'
  tags: scalar 1.3-err
  yaml: |
    ---word1
    word2
  tree: |
    +STR
     +DOC
      =VAL :---word1 word2
     -DOC
    -STR
  json: |
    "---word1 word2"
  dump: |
    '---word1 word2'
)RAW";

static constexpr std::string_view T_87E4 = R"RAW(
---
- name: Spec Example 7.8. Single Quoted Implicit Keys
  from: http://www.yaml.org/spec/1.2/spec.html#id2788496
  tags: spec flow sequence mapping
  yaml: |
    'implicit block key' : [
      'implicit flow key' : value,
     ]
  tree: |
    +STR
     +DOC
      +MAP
       =VAL 'implicit block key
       +SEQ []
        +MAP {}
         =VAL 'implicit flow key
         =VAL :value
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "implicit block key": [
        {
          "implicit flow key": "value"
        }
      ]
    }
  dump: |
    'implicit block key':
    - 'implicit flow key': value
)RAW";

static constexpr std::string_view T_8CWC = R"RAW(
---
- name: Plain mapping key ending with colon
  from: '@perlpunk'
  tags: mapping scalar
  yaml: |
    ---
    key ends with two colons::: value
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key ends with two colons::
       =VAL :value
      -MAP
     -DOC
    -STR
  json: |
    {
      "key ends with two colons::": "value"
    }
  dump: |
    ---
    'key ends with two colons::': value
)RAW";

static constexpr std::string_view T_8G76 = R"RAW(
---
- name: Spec Example 6.10. Comment Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2780544
  tags: spec comment empty scalar whitespace
  yaml: |2
      # Comment
    ␣␣␣
    ↵
    ↵
  tree: |
    +STR
    -STR
  json: ''
  dump: ''
)RAW";

static constexpr std::string_view T_8KB6 = R"RAW(
---
- name: Multiline plain flow mapping key without value
  from: '@perlpunk'
  tags: flow mapping
  yaml: |
    ---
    - { single line, a: b}
    - { multi
      line, a: b}
  tree: |
    +STR
     +DOC ---
      +SEQ
       +MAP {}
        =VAL :single line
        =VAL :
        =VAL :a
        =VAL :b
       -MAP
       +MAP {}
        =VAL :multi line
        =VAL :
        =VAL :a
        =VAL :b
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "single line": null,
        "a": "b"
      },
      {
        "multi line": null,
        "a": "b"
      }
    ]
  dump: |
    ---
    - single line:
      a: b
    - multi line:
      a: b
)RAW";

static constexpr std::string_view T_8MK2 = R"RAW(
---
- name: Explicit Non-Specific Tag
  from: NimYAML tests
  tags: tag 1.3-err
  yaml: |
    ! a
  tree: |
    +STR
     +DOC
      =VAL <!> :a
     -DOC
    -STR
  json: |
    "a"
)RAW";

static constexpr std::string_view T_8QBE = R"RAW(
---
- name: Block Sequence in Block Mapping
  from: NimYAML tests
  tags: mapping sequence
  yaml: |
    key:
     - item1
     - item2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +SEQ
        =VAL :item1
        =VAL :item2
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": [
        "item1",
        "item2"
      ]
    }
  dump: |
    key:
    - item1
    - item2
)RAW";

static constexpr std::string_view T_8UDB = R"RAW(
---
- name: Spec Example 7.14. Flow Sequence Entries
  from: http://www.yaml.org/spec/1.2/spec.html#id2790726
  tags: spec flow sequence
  yaml: |
    [
    "double
     quoted", 'single
               quoted',
    plain
     text, [ nested ],
    single: pair,
    ]
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL "double quoted
       =VAL 'single quoted
       =VAL :plain text
       +SEQ []
        =VAL :nested
       -SEQ
       +MAP {}
        =VAL :single
        =VAL :pair
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      "double quoted",
      "single quoted",
      "plain text",
      [
        "nested"
      ],
      {
        "single": "pair"
      }
    ]
  dump: |
    - "double quoted"
    - 'single quoted'
    - plain text
    - - nested
    - single: pair
)RAW";

static constexpr std::string_view T_8XDJ = R"RAW(
---
- name: Comment in plain multiline value
  from: https://gist.github.com/anonymous/deeb1ace28d5bf21fb56d80c13e2dc69 via @ingydotnet
  tags: error comment scalar
  fail: true
  yaml: |
    key: word1
    #  xxx
      word2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL :word1
)RAW";

static constexpr std::string_view T_8XYN = R"RAW(
---
- name: Anchor with unicode character
  from: https://github.com/yaml/pyyaml/issues/94
  tags: anchor
  yaml: |
    ---
    - &😁 unicode anchor
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL &😁 :unicode anchor
      -SEQ
     -DOC
    -STR
  json: |
    [
      "unicode anchor"
    ]
  dump: |
    ---
    - &😁 unicode anchor
)RAW";

static constexpr std::string_view T_93JH = R"RAW(
---
- name: Block Mappings in Block Sequence
  from: NimYAML tests
  tags: mapping sequence
  yaml: |2
     - key: value
       key2: value2
     -
       key3: value3
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :key
        =VAL :value
        =VAL :key2
        =VAL :value2
       -MAP
       +MAP
        =VAL :key3
        =VAL :value3
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "key": "value",
        "key2": "value2"
      },
      {
        "key3": "value3"
      }
    ]
  dump: |
    - key: value
      key2: value2
    - key3: value3
)RAW";

static constexpr std::string_view T_93WF = R"RAW(
---
- name: Spec Example 6.6. Line Folding [1.3]
  from: K527, modified for YAML 1.3
  tags: folded spec whitespace scalar 1.3-mod
  yaml: |
    --- >-
      trimmed
    ␣␣
    ␣

      as
      space
  tree: |
    +STR
     +DOC ---
      =VAL >trimmed\n\n\nas space
     -DOC
    -STR
  json: |
    "trimmed\n\n\nas space"
  dump: |
    --- >-
      trimmed



      as space
)RAW";

static constexpr std::string_view T_96L6 = R"RAW(
---
- name: Spec Example 2.14. In the folded scalars, newlines become spaces
  from: http://www.yaml.org/spec/1.2/spec.html#id2761032
  tags: spec folded scalar
  yaml: |
    --- >
      Mark McGwire's
      year was crippled
      by a knee injury.
  tree: |
    +STR
     +DOC ---
      =VAL >Mark McGwire's year was crippled by a knee injury.\n
     -DOC
    -STR
  json: |
    "Mark McGwire's year was crippled by a knee injury.\n"
  dump: |
    --- >
      Mark McGwire's year was crippled by a knee injury.
)RAW";

static constexpr std::string_view T_96NN = R"RAW(
---
- name: Leading tab content in literals
  from: '@ingydotnet'
  tags: indent literal whitespace
  yaml: |
    foo: |-
     ——»bar
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL |\tbar
      -MAP
     -DOC
    -STR
  json: |
    {"foo":"\tbar"}
  dump: |
    foo: |-
      ——»bar

- yaml: |
    foo: |-
     ——»bar∎
)RAW";

static constexpr std::string_view T_98YD = R"RAW(
---
- name: Spec Example 5.5. Comment Indicator
  from: http://www.yaml.org/spec/1.2/spec.html#id2773032
  tags: spec comment empty
  yaml: |
    # Comment only.
  tree: |
    +STR
    -STR
  json: ''
  dump: ''
)RAW";

static constexpr std::string_view T_9BXH = R"RAW(
---
- name: Multiline doublequoted flow mapping key without value
  from: '@perlpunk'
  tags: double flow mapping
  yaml: |
    ---
    - { "single line", a: b}
    - { "multi
      line", a: b}
  tree: |
    +STR
     +DOC ---
      +SEQ
       +MAP {}
        =VAL "single line
        =VAL :
        =VAL :a
        =VAL :b
       -MAP
       +MAP {}
        =VAL "multi line
        =VAL :
        =VAL :a
        =VAL :b
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "single line": null,
        "a": "b"
      },
      {
        "multi line": null,
        "a": "b"
      }
    ]
  dump: |
    ---
    - "single line":
      a: b
    - "multi line":
      a: b
)RAW";

static constexpr std::string_view T_9C9N = R"RAW(
---
- name: Wrong indented flow sequence
  from: '@perlpunk'
  tags: error flow indent sequence
  fail: true
  yaml: |
    ---
    flow: [a,
    b,
    c]
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :flow
       +SEQ []
        =VAL :a
)RAW";

static constexpr std::string_view T_9CWY = R"RAW(
---
- name: Invalid scalar at the end of mapping
  from: '@perlpunk'
  tags: error mapping sequence
  fail: true
  yaml: |
    key:
     - item1
     - item2
    invalid
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +SEQ
        =VAL :item1
        =VAL :item2
       -SEQ
)RAW";

static constexpr std::string_view T_9DXL = R"RAW(
---
- name: Spec Example 9.6. Stream [1.3]
  from: 6ZKB, modified for YAML 1.3
  tags: spec header 1.3-mod
  yaml: |
    Mapping: Document
    ---
    # Empty
    ...
    %YAML 1.2
    ---
    matches %: 20
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :Mapping
       =VAL :Document
      -MAP
     -DOC
     +DOC ---
      =VAL :
     -DOC ...
     +DOC ---
      +MAP
       =VAL :matches %
       =VAL :20
      -MAP
     -DOC
    -STR
  json: |
    {
      "Mapping": "Document"
    }
    null
    {
      "matches %": 20
    }
  emit: |
    Mapping: Document
    ---
    ...
    %YAML 1.2
    ---
    matches %: 20
)RAW";

static constexpr std::string_view T_9FMG = R"RAW(
---
- name: Multi-level Mapping Indent
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/indent.tml
  tags: mapping indent
  yaml: |
    a:
      b:
        c: d
      e:
        f: g
    h: i
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       +MAP
        =VAL :b
        +MAP
         =VAL :c
         =VAL :d
        -MAP
        =VAL :e
        +MAP
         =VAL :f
         =VAL :g
        -MAP
       -MAP
       =VAL :h
       =VAL :i
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": {
        "b": {
          "c": "d"
        },
        "e": {
          "f": "g"
        }
      },
      "h": "i"
    }
)RAW";

static constexpr std::string_view T_9HCY = R"RAW(
---
- name: Need document footer before directives
  from: '@ingydotnet'
  tags: directive error footer tag unknown-tag
  fail: true
  yaml: |
    !foo "bar"
    %TAG ! tag:example.com,2000:app/
    ---
    !foo "bar"
  tree: |
    +STR
     +DOC
      =VAL <!foo> "bar
)RAW";

static constexpr std::string_view T_9J7A = R"RAW(
---
- name: Simple Mapping Indent
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/indent.tml
  tags: simple mapping indent
  yaml: |
    foo:
      bar: baz
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       +MAP
        =VAL :bar
        =VAL :baz
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": {
        "bar": "baz"
      }
    }
)RAW";

static constexpr std::string_view T_9JBA = R"RAW(
---
- name: Invalid comment after end of flow sequence
  from: '@perlpunk'
  tags: comment error flow sequence
  fail: true
  yaml: |
    ---
    [ a, b, c, ]#invalid
  tree: |
    +STR
     +DOC ---
      +SEQ []
       =VAL :a
       =VAL :b
       =VAL :c
      -SEQ
)RAW";

static constexpr std::string_view T_9KAX = R"RAW(
---
- name: Various combinations of tags and anchors
  from: '@perlpunk'
  tags: anchor mapping 1.3-err tag
  yaml: |
    ---
    &a1
    !!str
    scalar1
    ---
    !!str
    &a2
    scalar2
    ---
    &a3
    !!str scalar3
    ---
    &a4 !!map
    &a5 !!str key5: value4
    ---
    a6: 1
    &anchor6 b6: 2
    ---
    !!map
    &a8 !!str key8: value7
    ---
    !!map
    !!str &a10 key10: value9
    ---
    !!str &a11
    value11
  tree: |
    +STR
     +DOC ---
      =VAL &a1 <tag:yaml.org,2002:str> :scalar1
     -DOC
     +DOC ---
      =VAL &a2 <tag:yaml.org,2002:str> :scalar2
     -DOC
     +DOC ---
      =VAL &a3 <tag:yaml.org,2002:str> :scalar3
     -DOC
     +DOC ---
      +MAP &a4 <tag:yaml.org,2002:map>
       =VAL &a5 <tag:yaml.org,2002:str> :key5
       =VAL :value4
      -MAP
     -DOC
     +DOC ---
      +MAP
       =VAL :a6
       =VAL :1
       =VAL &anchor6 :b6
       =VAL :2
      -MAP
     -DOC
     +DOC ---
      +MAP <tag:yaml.org,2002:map>
       =VAL &a8 <tag:yaml.org,2002:str> :key8
       =VAL :value7
      -MAP
     -DOC
     +DOC ---
      +MAP <tag:yaml.org,2002:map>
       =VAL &a10 <tag:yaml.org,2002:str> :key10
       =VAL :value9
      -MAP
     -DOC
     +DOC ---
      =VAL &a11 <tag:yaml.org,2002:str> :value11
     -DOC
    -STR
  json: |
    "scalar1"
    "scalar2"
    "scalar3"
    {
      "key5": "value4"
    }
    {
      "a6": 1,
      "b6": 2
    }
    {
      "key8": "value7"
    }
    {
      "key10": "value9"
    }
    "value11"
  dump: |
    --- &a1 !!str scalar1
    --- &a2 !!str scalar2
    --- &a3 !!str scalar3
    --- &a4 !!map
    &a5 !!str key5: value4
    ---
    a6: 1
    &anchor6 b6: 2
    --- !!map
    &a8 !!str key8: value7
    --- !!map
    &a10 !!str key10: value9
    --- &a11 !!str value11
)RAW";

static constexpr std::string_view T_9KBC = R"RAW(
---
- name: Mapping starting at --- line
  from: https://gist.github.com/anonymous/c728390e92ec93fb371ac77f21435cca via @ingydotnet
  tags: error header mapping
  fail: true
  yaml: |
    --- key1: value1
        key2: value2
  tree: |
    +STR
     +DOC ---
)RAW";

static constexpr std::string_view T_9MAG = R"RAW(
---
- name: Flow sequence with invalid comma at the beginning
  from: '@perlpunk'
  tags: error flow sequence
  fail: true
  yaml: |
    ---
    [ , a, b, c ]
  tree: |
    +STR
     +DOC ---
      +SEQ []
)RAW";

static constexpr std::string_view T_9MMA = R"RAW(
---
- name: Directive by itself with no document
  from: '@ingydotnet'
  tags: error directive
  fail: true
  yaml: |
    %YAML 1.2
  tree: |
    +STR
)RAW";

static constexpr std::string_view T_9MMW = R"RAW(
---
- name: Single Pair Implicit Entries
  from: '@perlpunk, Spec Example 7.21'
  tags: flow mapping sequence
  yaml: |
    - [ YAML : separate ]
    - [ "JSON like":adjacent ]
    - [ {JSON: like}:adjacent ]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        +MAP {}
         =VAL :YAML
         =VAL :separate
        -MAP
       -SEQ
       +SEQ []
        +MAP {}
         =VAL "JSON like
         =VAL :adjacent
        -MAP
       -SEQ
       +SEQ []
        +MAP {}
         +MAP {}
          =VAL :JSON
          =VAL :like
         -MAP
         =VAL :adjacent
        -MAP
       -SEQ
      -SEQ
     -DOC
    -STR
  dump: |
    - - YAML: separate
    - - "JSON like": adjacent
    - - ? JSON: like
        : adjacent
)RAW";

static constexpr std::string_view T_9MQT = R"RAW(
---
- name: Scalar doc with '...' in content
  from: '@ingydotnet'
  tags: double scalar
  yaml: |
    --- "a
    ...x
    b"
  tree: |
    +STR
     +DOC ---
      =VAL "a ...x b
     -DOC
    -STR
  json: |
    "a ...x b"
  dump: |
    --- a ...x b
  emit: |
    --- "a ...x b"

- fail: true
  yaml: |
    --- "a
    ... x
    b"
  tree: |
    +STR
     +DOC ---
  dump: null
  emit: null
)RAW";

static constexpr std::string_view T_9SA2 = R"RAW(
---
- name: Multiline double quoted flow mapping key
  from: '@perlpunk'
  tags: double flow mapping
  yaml: |
    ---
    - { "single line": value}
    - { "multi
      line": value}
  tree: |
    +STR
     +DOC ---
      +SEQ
       +MAP {}
        =VAL "single line
        =VAL :value
       -MAP
       +MAP {}
        =VAL "multi line
        =VAL :value
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "single line": "value"
      },
      {
        "multi line": "value"
      }
    ]
  dump: |
    ---
    - "single line": value
    - "multi line": value
)RAW";

static constexpr std::string_view T_9SHH = R"RAW(
---
- name: Spec Example 5.8. Quoted Scalar Indicators
  from: http://www.yaml.org/spec/1.2/spec.html#id2773890
  tags: spec scalar
  yaml: |
    single: 'text'
    double: "text"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :single
       =VAL 'text
       =VAL :double
       =VAL "text
      -MAP
     -DOC
    -STR
  json: |
    {
      "single": "text",
      "double": "text"
    }
)RAW";

static constexpr std::string_view T_9TFX = R"RAW(
---
- name: Spec Example 7.6. Double Quoted Lines [1.3]
  from: 7A4E, modified for YAML 1.3
  tags: double spec scalar whitespace 1.3-mod
  yaml: |
    ---
    " 1st non-empty

     2nd non-empty␣
     3rd non-empty "
  tree: |
    +STR
     +DOC ---
      =VAL " 1st non-empty\n2nd non-empty 3rd non-empty␣
     -DOC
    -STR
  json: |
    " 1st non-empty\n2nd non-empty 3rd non-empty "
  dump: |
    " 1st non-empty\n2nd non-empty 3rd non-empty "
  emit: |
    --- " 1st non-empty\n2nd non-empty 3rd non-empty "
)RAW";

static constexpr std::string_view T_9U5K = R"RAW(
---
- name: Spec Example 2.12. Compact Nested Mapping
  from: http://www.yaml.org/spec/1.2/spec.html#id2760821
  tags: spec mapping sequence
  yaml: |
    ---
    # Products purchased
    - item    : Super Hoop
      quantity: 1
    - item    : Basketball
      quantity: 4
    - item    : Big Shoes
      quantity: 1
  tree: |
    +STR
     +DOC ---
      +SEQ
       +MAP
        =VAL :item
        =VAL :Super Hoop
        =VAL :quantity
        =VAL :1
       -MAP
       +MAP
        =VAL :item
        =VAL :Basketball
        =VAL :quantity
        =VAL :4
       -MAP
       +MAP
        =VAL :item
        =VAL :Big Shoes
        =VAL :quantity
        =VAL :1
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "item": "Super Hoop",
        "quantity": 1
      },
      {
        "item": "Basketball",
        "quantity": 4
      },
      {
        "item": "Big Shoes",
        "quantity": 1
      }
    ]
  dump: |
    ---
    - item: Super Hoop
      quantity: 1
    - item: Basketball
      quantity: 4
    - item: Big Shoes
      quantity: 1
)RAW";

static constexpr std::string_view T_9WXW = R"RAW(
---
- name: Spec Example 6.18. Primary Tag Handle
  from: http://www.yaml.org/spec/1.2/spec.html#id2782728
  tags: local-tag spec directive tag unknown-tag 1.3-err
  yaml: |
    # Private
    !foo "bar"
    ...
    # Global
    %TAG ! tag:example.com,2000:app/
    ---
    !foo "bar"
  tree: |
    +STR
     +DOC
      =VAL <!foo> "bar
     -DOC ...
     +DOC ---
      =VAL <tag:example.com,2000:app/foo> "bar
     -DOC
    -STR
  json: |
    "bar"
    "bar"
  dump: |
    !foo "bar"
    ...
    --- !<tag:example.com,2000:app/foo> "bar"
)RAW";

static constexpr std::string_view T_9YRD = R"RAW(
---
- name: Multiline Scalar at Top Level
  from: NimYAML tests
  tags: scalar whitespace 1.3-err
  yaml: |
    a
    b␣␣
      c
    d

    e
  tree: |
    +STR
     +DOC
      =VAL :a b c d\ne
     -DOC
    -STR
  json: |
    "a b c d\ne"
  dump: |
    'a b c d

      e'
)RAW";

static constexpr std::string_view T_A2M4 = R"RAW(
---
- name: Spec Example 6.2. Indentation Indicators
  from: http://www.yaml.org/spec/1.2/spec.html#id2778101
  tags: explicit-key spec libyaml-err indent whitespace sequence upto-1.2
  yaml: |
    ? a
    : -»b
      -  -—»c
         - d
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       +SEQ
        =VAL :b
        +SEQ
         =VAL :c
         =VAL :d
        -SEQ
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": [
        "b",
        [
          "c",
          "d"
        ]
      ]
    }
  dump: |
    a:
    - b
    - - c
      - d
)RAW";

static constexpr std::string_view T_A6F9 = R"RAW(
---
- name: Spec Example 8.4. Chomping Final Line Break
  from: http://www.yaml.org/spec/1.2/spec.html#id2795034
  tags: spec literal scalar
  yaml: |
    strip: |-
      text
    clip: |
      text
    keep: |+
      text
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :strip
       =VAL |text
       =VAL :clip
       =VAL |text\n
       =VAL :keep
       =VAL |text\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "strip": "text",
      "clip": "text\n",
      "keep": "text\n"
    }
  dump: |
    strip: |-
      text
    clip: |
      text
    keep: |
      text
)RAW";

static constexpr std::string_view T_A984 = R"RAW(
---
- name: Multiline Scalar in Mapping
  from: NimYAML tests
  tags: scalar
  yaml: |
    a: b
     c
    d:
     e
      f
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL :b c
       =VAL :d
       =VAL :e f
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": "b c",
      "d": "e f"
    }
  dump: |
    a: b c
    d: e f
)RAW";

static constexpr std::string_view T_AB8U = R"RAW(
---
- name: Sequence entry that looks like two with wrong indentation
  from: '@perlpunk'
  tags: scalar sequence
  yaml: |
    - single multiline
     - sequence entry
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :single multiline - sequence entry
      -SEQ
     -DOC
    -STR
  json: |
    [
      "single multiline - sequence entry"
    ]
  dump: |
    - single multiline - sequence entry
)RAW";

static constexpr std::string_view T_AVM7 = R"RAW(
---
- name: Empty Stream
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/misc.tml
  tags: edge
  yaml: |
    ∎
  tree: |
    +STR
    -STR
  json: ''
)RAW";

static constexpr std::string_view T_AZ63 = R"RAW(
---
- name: Sequence With Same Indentation as Parent Mapping
  from: NimYAML tests
  tags: indent mapping sequence
  yaml: |
    one:
    - 2
    - 3
    four: 5
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :one
       +SEQ
        =VAL :2
        =VAL :3
       -SEQ
       =VAL :four
       =VAL :5
      -MAP
     -DOC
    -STR
  json: |
    {
      "one": [
        2,
        3
      ],
      "four": 5
    }
)RAW";

static constexpr std::string_view T_AZW3 = R"RAW(
---
- name: Lookahead test cases
  from: NimYAML tests
  tags: mapping edge
  yaml: |
    - bla"keks: foo
    - bla]keks: foo
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :bla"keks
        =VAL :foo
       -MAP
       +MAP
        =VAL :bla]keks
        =VAL :foo
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "bla\"keks": "foo"
      },
      {
        "bla]keks": "foo"
      }
    ]
)RAW";

static constexpr std::string_view T_B3HG = R"RAW(
---
- name: Spec Example 8.9. Folded Scalar [1.3]
  from: G992, modified for YAML 1.3
  tags: spec folded scalar 1.3-mod
  yaml: |
    --- >
     folded
     text
    ↵
    ↵
  tree: |
    +STR
     +DOC ---
      =VAL >folded text\n
     -DOC
    -STR
  json: |
    "folded text\n"
  dump: |
    >
      folded text
  emit: |
    --- >
      folded text
)RAW";

static constexpr std::string_view T_B63P = R"RAW(
---
- name: Directive without document
  from: AdaYaml tests
  tags: error directive document
  fail: true
  yaml: |
    %YAML 1.2
    ...
  tree: |
    +STR
)RAW";

static constexpr std::string_view T_BD7L = R"RAW(
---
- name: Invalid mapping after sequence
  from: '@perlpunk'
  tags: error mapping sequence
  fail: true
  yaml: |
    - item1
    - item2
    invalid: x
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :item1
       =VAL :item2
)RAW";

static constexpr std::string_view T_BEC7 = R"RAW(
---
- name: Spec Example 6.14. “YAML” directive
  from: http://www.yaml.org/spec/1.2/spec.html#id2781929
  tags: spec directive
  yaml: |
    %YAML 1.3 # Attempt parsing
              # with a warning
    ---
    "foo"
  tree: |
    +STR
     +DOC ---
      =VAL "foo
     -DOC
    -STR
  json: |
    "foo"
  dump: |
    --- "foo"
)RAW";

static constexpr std::string_view T_BF9H = R"RAW(
---
- name: Trailing comment in multiline plain scalar
  from: '@perlpunk'
  tags: comment error scalar
  fail: true
  yaml: |
    ---
    plain: a
           b # end of scalar
           c
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :plain
       =VAL :a b
)RAW";

static constexpr std::string_view T_BS4K = R"RAW(
---
- name: Comment between plain scalar lines
  from: https://gist.github.com/anonymous/269f16d582fdd30a7dcf8c9249c5da7f via @ingydotnet
  tags: error scalar
  fail: true
  yaml: |
    word1  # comment
    word2
  tree: |
    +STR
     +DOC
      =VAL :word1
     -DOC
)RAW";

static constexpr std::string_view T_BU8L = R"RAW(
---
- name: Node Anchor and Tag on Seperate Lines
  from: https://gist.github.com/anonymous/f192e7dab6da31831f264dbf1947cb83 via @ingydotnet
  tags: anchor indent 1.3-err tag
  yaml: |
    key: &anchor
     !!map
      a: b
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +MAP &anchor <tag:yaml.org,2002:map>
        =VAL :a
        =VAL :b
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": {
        "a": "b"
      }
    }
  dump: |
    key: &anchor !!map
      a: b
)RAW";

static constexpr std::string_view T_C2DT = R"RAW(
---
- name: Spec Example 7.18. Flow Mapping Adjacent Values
  from: http://www.yaml.org/spec/1.2/spec.html#id2792073
  tags: spec flow mapping
  yaml: |
    {
    "adjacent":value,
    "readable": value,
    "empty":
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL "adjacent
       =VAL :value
       =VAL "readable
       =VAL :value
       =VAL "empty
       =VAL :
      -MAP
     -DOC
    -STR
  json: |
    {
      "adjacent": "value",
      "readable": "value",
      "empty": null
    }
  dump: |
    "adjacent": value
    "readable": value
    "empty":
)RAW";

static constexpr std::string_view T_C2SP = R"RAW(
---
- name: Flow Mapping Key on two lines
  from: '@perlpunk'
  tags: error flow mapping
  fail: true
  yaml: |
    [23
    ]: 42
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL :23
)RAW";

static constexpr std::string_view T_C4HZ = R"RAW(
---
- name: Spec Example 2.24. Global Tags
  from: http://www.yaml.org/spec/1.2/spec.html#id2761719
  tags: spec tag alias directive local-tag
  yaml: |
    %TAG ! tag:clarkevans.com,2002:
    --- !shape
      # Use the ! handle for presenting
      # tag:clarkevans.com,2002:circle
    - !circle
      center: &ORIGIN {x: 73, y: 129}
      radius: 7
    - !line
      start: *ORIGIN
      finish: { x: 89, y: 102 }
    - !label
      start: *ORIGIN
      color: 0xFFEEBB
      text: Pretty vector drawing.
  tree: |
    +STR
     +DOC ---
      +SEQ <tag:clarkevans.com,2002:shape>
       +MAP <tag:clarkevans.com,2002:circle>
        =VAL :center
        +MAP {} &ORIGIN
         =VAL :x
         =VAL :73
         =VAL :y
         =VAL :129
        -MAP
        =VAL :radius
        =VAL :7
       -MAP
       +MAP <tag:clarkevans.com,2002:line>
        =VAL :start
        =ALI *ORIGIN
        =VAL :finish
        +MAP {}
         =VAL :x
         =VAL :89
         =VAL :y
         =VAL :102
        -MAP
       -MAP
       +MAP <tag:clarkevans.com,2002:label>
        =VAL :start
        =ALI *ORIGIN
        =VAL :color
        =VAL :0xFFEEBB
        =VAL :text
        =VAL :Pretty vector drawing.
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "center": {
          "x": 73,
          "y": 129
        },
        "radius": 7
      },
      {
        "start": {
          "x": 73,
          "y": 129
        },
        "finish": {
          "x": 89,
          "y": 102
        }
      },
      {
        "start": {
          "x": 73,
          "y": 129
        },
        "color": 16772795,
        "text": "Pretty vector drawing."
      }
    ]
  dump: |
    --- !<tag:clarkevans.com,2002:shape>
    - !<tag:clarkevans.com,2002:circle>
      center: &ORIGIN
        x: 73
        y: 129
      radius: 7
    - !<tag:clarkevans.com,2002:line>
      start: *ORIGIN
      finish:
        x: 89
        y: 102
    - !<tag:clarkevans.com,2002:label>
      start: *ORIGIN
      color: 0xFFEEBB
      text: Pretty vector drawing.
)RAW";

static constexpr std::string_view T_CC74 = R"RAW(
---
- name: Spec Example 6.20. Tag Handles
  from: http://www.yaml.org/spec/1.2/spec.html#id2783195
  tags: spec directive tag unknown-tag
  yaml: |
    %TAG !e! tag:example.com,2000:app/
    ---
    !e!foo "bar"
  tree: |
    +STR
     +DOC ---
      =VAL <tag:example.com,2000:app/foo> "bar
     -DOC
    -STR
  json: |
    "bar"
  dump: |
    --- !<tag:example.com,2000:app/foo> "bar"
)RAW";

static constexpr std::string_view T_CFD4 = R"RAW(
---
- name: Empty implicit key in single pair flow sequences
  from: '@perlpunk'
  tags: empty-key flow sequence
  yaml: |
    - [ : empty key ]
    - [: another empty key]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        +MAP {}
         =VAL :
         =VAL :empty key
        -MAP
       -SEQ
       +SEQ []
        +MAP {}
         =VAL :
         =VAL :another empty key
        -MAP
       -SEQ
      -SEQ
     -DOC
    -STR
  dump: |
    - - : empty key
    - - : another empty key
)RAW";

static constexpr std::string_view T_CML9 = R"RAW(
---
- name: Missing comma in flow
  from: ihttps://gist.github.com/anonymous/4ba3365607cc14b4f656e391b45bf4f4 via @ingydotnet
  tags: error flow comment
  fail: true
  yaml: |
    key: [ word1
    #  xxx
      word2 ]
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +SEQ []
        =VAL :word1
)RAW";

static constexpr std::string_view T_CN3R = R"RAW(
---
- name: Various location of anchors in flow sequence
  from: '@perlpunk'
  tags: anchor flow mapping sequence
  yaml: |
    &flowseq [
     a: b,
     &c c: d,
     { &e e: f },
     &g { g: h }
    ]
  tree: |
    +STR
     +DOC
      +SEQ [] &flowseq
       +MAP {}
        =VAL :a
        =VAL :b
       -MAP
       +MAP {}
        =VAL &c :c
        =VAL :d
       -MAP
       +MAP {}
        =VAL &e :e
        =VAL :f
       -MAP
       +MAP {} &g
        =VAL :g
        =VAL :h
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "a": "b"
      },
      {
        "c": "d"
      },
      {
        "e": "f"
      },
      {
        "g": "h"
      }
    ]
  dump: |
    &flowseq
    - a: b
    - &c c: d
    - &e e: f
    - &g
      g: h
)RAW";

static constexpr std::string_view T_CPZ3 = R"RAW(
---
- name: Doublequoted scalar starting with a tab
  from: '@perlpunk'
  tags: double scalar
  yaml: |
    ---
    tab: "\tstring"
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :tab
       =VAL "\tstring
      -MAP
     -DOC
    -STR
  json: |
    {
      "tab": "\tstring"
    }
  dump: |
    ---
    tab: "\tstring"
)RAW";

static constexpr std::string_view T_CQ3W = R"RAW(
---
- name: Double quoted string without closing quote
  from: '@perlpunk'
  tags: error double
  fail: true
  yaml: |
    ---
    key: "missing closing quote
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key
)RAW";

static constexpr std::string_view T_CT4Q = R"RAW(
---
- name: Spec Example 7.20. Single Pair Explicit Entry
  from: http://www.yaml.org/spec/1.2/spec.html#id2792424
  tags: explicit-key spec flow mapping
  yaml: |
    [
    ? foo
     bar : baz
    ]
  tree: |
    +STR
     +DOC
      +SEQ []
       +MAP {}
        =VAL :foo bar
        =VAL :baz
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "foo bar": "baz"
      }
    ]
  dump: |
    - foo bar: baz
)RAW";

static constexpr std::string_view T_CTN5 = R"RAW(
---
- name: Flow sequence with invalid extra comma
  from: '@perlpunk'
  tags: error flow sequence
  fail: true
  yaml: |
    ---
    [ a, b, c, , ]
  tree: |
    +STR
     +DOC ---
      +SEQ []
       =VAL :a
       =VAL :b
       =VAL :c
)RAW";

static constexpr std::string_view T_CUP7 = R"RAW(
---
- name: Spec Example 5.6. Node Property Indicators
  from: http://www.yaml.org/spec/1.2/spec.html#id2773402
  tags: local-tag spec tag alias
  yaml: |
    anchored: !local &anchor value
    alias: *anchor
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :anchored
       =VAL &anchor <!local> :value
       =VAL :alias
       =ALI *anchor
      -MAP
     -DOC
    -STR
  json: |
    {
      "anchored": "value",
      "alias": "value"
    }
  dump: |
    anchored: &anchor !local value
    alias: *anchor
)RAW";

static constexpr std::string_view T_CVW2 = R"RAW(
---
- name: Invalid comment after comma
  from: '@perlpunk'
  tags: comment error flow sequence
  fail: true
  yaml: |
    ---
    [ a, b, c,#invalid
    ]
  tree: |
    +STR
     +DOC ---
      +SEQ []
       =VAL :a
       =VAL :b
       =VAL :c
)RAW";

static constexpr std::string_view T_CXX2 = R"RAW(
---
- name: Mapping with anchor on document start line
  from: '@perlpunk'
  tags: anchor error header mapping
  fail: true
  yaml: |
    --- &anchor a: b
  tree: |
    +STR
     +DOC ---
)RAW";

static constexpr std::string_view T_D49Q = R"RAW(
---
- name: Multiline single quoted implicit keys
  from: '@perlpunk'
  tags: error single mapping
  fail: true
  yaml: |
    'a\nb': 1
    'c
     d': 1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL 'a\\nb
       =VAL :1
)RAW";

static constexpr std::string_view T_D83L = R"RAW(
---
- name: Block scalar indicator order
  from: '@perlpunk'
  tags: indent literal
  yaml: |
    - |2-
      explicit indent and chomp
    - |-2
      chomp and explicit indent
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |explicit indent and chomp
       =VAL |chomp and explicit indent
      -SEQ
     -DOC
    -STR
  json: |
    [
      "explicit indent and chomp",
      "chomp and explicit indent"
    ]
  dump: |
    - |-
      explicit indent and chomp
    - |-
      chomp and explicit indent
)RAW";

static constexpr std::string_view T_D88J = R"RAW(
---
- name: Flow Sequence in Block Mapping
  from: NimYAML tests
  tags: flow sequence mapping
  yaml: |
    a: [b, c]
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       +SEQ []
        =VAL :b
        =VAL :c
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": [
        "b",
        "c"
      ]
    }
  dump: |
    a:
    - b
    - c
)RAW";

static constexpr std::string_view T_D9TU = R"RAW(
---
- name: Single Pair Block Mapping
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/mapping.tml
  tags: simple mapping
  yaml: |
    foo: bar
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "bar"
    }
)RAW";

static constexpr std::string_view T_DBG4 = R"RAW(
---
- name: Spec Example 7.10. Plain Characters
  from: http://www.yaml.org/spec/1.2/spec.html#id2789510
  tags: spec flow sequence scalar
  yaml: |
    # Outside flow collection:
    - ::vector
    - ": - ()"
    - Up, up, and away!
    - -123
    - http://example.com/foo#bar
    # Inside flow collection:
    - [ ::vector,
      ": - ()",
      "Up, up and away!",
      -123,
      http://example.com/foo#bar ]
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :::vector
       =VAL ": - ()
       =VAL :Up, up, and away!
       =VAL :-123
       =VAL :http://example.com/foo#bar
       +SEQ []
        =VAL :::vector
        =VAL ": - ()
        =VAL "Up, up and away!
        =VAL :-123
        =VAL :http://example.com/foo#bar
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      "::vector",
      ": - ()",
      "Up, up, and away!",
      -123,
      "http://example.com/foo#bar",
      [
        "::vector",
        ": - ()",
        "Up, up and away!",
        -123,
        "http://example.com/foo#bar"
      ]
    ]
  dump: |
    - ::vector
    - ": - ()"
    - Up, up, and away!
    - -123
    - http://example.com/foo#bar
    - - ::vector
      - ": - ()"
      - "Up, up and away!"
      - -123
      - http://example.com/foo#bar
)RAW";

static constexpr std::string_view T_DC7X = R"RAW(
---
- name: Various trailing tabs
  from: '@perlpunk'
  tags: comment whitespace
  yaml: |
    a: b———»
    seq:———»
     - a———»
    c: d———»#X
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL :b
       =VAL :seq
       +SEQ
        =VAL :a
       -SEQ
       =VAL :c
       =VAL :d
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": "b",
      "seq": [
        "a"
      ],
      "c": "d"
    }
  dump: |
    a: b
    seq:
    - a
    c: d
)RAW";

static constexpr std::string_view T_DE56 = R"RAW(
---
- name: Trailing tabs in double quoted
  from: '@ingydotnet'
  tags: double whitespace
  yaml: |
    "1 trailing\t
        tab"
  tree: |
    +STR
     +DOC
      =VAL "1 trailing\t tab
     -DOC
    -STR
  json: |
    "1 trailing\t tab"
  dump: |
    "1 trailing\t tab"

- yaml: |
    "2 trailing\t␣␣
        tab"
  tree: |
    +STR
     +DOC
      =VAL "2 trailing\t tab
     -DOC
    -STR
  json: |
    "2 trailing\t tab"
  dump: |
    "2 trailing\t tab"

- yaml: |
    "3 trailing\————»
        tab"
  tree: |
    +STR
     +DOC
      =VAL "3 trailing\t tab
     -DOC
    -STR
  json: |
    "3 trailing\t tab"
  dump: |
    "3 trailing\t tab"

- yaml: |
    "4 trailing\————»␣␣
        tab"
  tree: |
    +STR
     +DOC
      =VAL "4 trailing\t tab
     -DOC
    -STR
  json: |
    "4 trailing\t tab"
  dump: |
    "4 trailing\t tab"

- yaml: |
    "5 trailing—»
        tab"
  tree: |
    +STR
     +DOC
      =VAL "5 trailing tab
     -DOC
    -STR
  json: |
    "5 trailing tab"
  dump: |
    "5 trailing tab"

- yaml: |
    "6 trailing—»␣␣
        tab"
  tree: |
    +STR
     +DOC
      =VAL "6 trailing tab
     -DOC
    -STR
  json: |
    "6 trailing tab"
  dump: |
    "6 trailing tab"
)RAW";

static constexpr std::string_view T_DFF7 = R"RAW(
---
- name: Spec Example 7.16. Flow Mapping Entries
  from: http://www.yaml.org/spec/1.2/spec.html#id2791260
  tags: explicit-key spec flow mapping
  yaml: |
    {
    ? explicit: entry,
    implicit: entry,
    ?
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :explicit
       =VAL :entry
       =VAL :implicit
       =VAL :entry
       =VAL :
       =VAL :
      -MAP
     -DOC
    -STR
  dump: |
    explicit: entry
    implicit: entry
    :
)RAW";

static constexpr std::string_view T_DHP8 = R"RAW(
---
- name: Flow Sequence
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/sequence.tml
  tags: flow sequence
  yaml: |
    [foo, bar, 42]
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL :foo
       =VAL :bar
       =VAL :42
      -SEQ
     -DOC
    -STR
  json: |
    [
      "foo",
      "bar",
      42
    ]
  dump: |
    - foo
    - bar
    - 42
)RAW";

static constexpr std::string_view T_DK3J = R"RAW(
---
- name: Zero indented block scalar with line that looks like a comment
  from: '@perlpunk'
  tags: comment folded scalar
  yaml: |
    --- >
    line1
    # no comment
    line3
  tree: |
    +STR
     +DOC ---
      =VAL >line1 # no comment line3\n
     -DOC
    -STR
  json: |
    "line1 # no comment line3\n"
  dump: |
    --- >
      line1 # no comment line3
)RAW";

static constexpr std::string_view T_DK4H = R"RAW(
---
- name: Implicit key followed by newline
  from: '@perlpunk'
  tags: error flow mapping sequence
  fail: true
  yaml: |
    ---
    [ key
      : value ]
  tree: |
    +STR
     +DOC ---
      +SEQ []
       =VAL :key
)RAW";

static constexpr std::string_view T_DK95 = R"RAW(
---
- name: Tabs that look like indentation
  from: '@ingydotnet'
  tags: indent whitespace
  yaml: |
    foo:
     ———»bar
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : "bar"
    }
  emit: |
    ---
    foo: bar

- fail: true
  yaml: |
    foo: "bar
    ————»baz"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo

- yaml: |
    foo: "bar
      ——»baz"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL "bar baz
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : "bar baz"
    }
  emit: |
    ---
    foo: "bar baz"

- yaml: |2
     ———»
    foo: 1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :1
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : 1
    }
  emit: |
    ---
    foo: 1

- yaml: |
    foo: 1
    ————»
    bar: 2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :1
       =VAL :bar
       =VAL :2
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : 1,
      "bar" : 2
    }
  emit: |
    ---
    foo: 1
    bar: 2

- yaml: |
    foo: 1
     ———»
    bar: 2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :1
       =VAL :bar
       =VAL :2
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : 1,
      "bar" : 2
    }
  emit: |
    ---
    foo: 1
    bar: 2

- fail: true
  yaml: |
    foo:
      a: 1
      ——»b: 2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       +MAP
        =VAL :a
        =VAL :1

- yaml: |
    %YAML 1.2
    ————»
    ---
  tree: |
    +STR
     +DOC ---
      =VAL :
     -DOC
    -STR
  json: |
    null
  emit: |
    --- null

- yaml: |
    foo: "bar
     ———» ——» baz ——» ——» "
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL "bar baz \t \t␣
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : "bar baz \t \t "
    }
  emit: |
    ---
    foo: "bar baz \t \t "
)RAW";

static constexpr std::string_view T_DMG6 = R"RAW(
---
- name: Wrong indendation in Map
  from: '@perlpunk'
  tags: error mapping indent
  fail: true
  yaml: |
    key:
      ok: 1
     wrong: 2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       +MAP
        =VAL :ok
        =VAL :1
       -MAP
)RAW";

static constexpr std::string_view T_DWX9 = R"RAW(
---
- name: Spec Example 8.8. Literal Content
  from: http://www.yaml.org/spec/1.2/spec.html#id2796118
  tags: spec literal scalar comment whitespace 1.3-err
  yaml: |
    |
    ␣
    ␣␣
      literal
    ␣␣␣
    ␣␣
      text

     # Comment
  tree: |
    +STR
     +DOC
      =VAL |\n\nliteral\n \n\ntext\n
     -DOC
    -STR
  json: |
    "\n\nliteral\n \n\ntext\n"
  dump: |
    "\n\nliteral\n \n\ntext\n"
  emit: |
    |


      literal
    ␣␣␣

      text
)RAW";

static constexpr std::string_view T_E76Z = R"RAW(
---
- name: Aliases in Implicit Block Mapping
  from: NimYAML tests
  tags: mapping alias
  yaml: |
    &a a: &b b
    *b : *a
  tree: |
    +STR
     +DOC
      +MAP
       =VAL &a :a
       =VAL &b :b
       =ALI *b
       =ALI *a
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": "b",
      "b": "a"
    }
  dump: |
    &a a: &b b
    *b : *a
)RAW";

static constexpr std::string_view T_EB22 = R"RAW(
---
- name: Missing document-end marker before directive
  from: '@perlpunk'
  tags: error directive footer
  fail: true
  yaml: |
    ---
    scalar1 # comment
    %YAML 1.2
    ---
    scalar2
  tree: |
    +STR
     +DOC ---
      =VAL :scalar1
     -DOC
)RAW";

static constexpr std::string_view T_EHF6 = R"RAW(
---
- name: Tags for Flow Objects
  from: NimYAML tests
  tags: tag flow mapping sequence
  yaml: |
    !!map {
      k: !!seq
      [ a, !!str b]
    }
  tree: |
    +STR
     +DOC
      +MAP {} <tag:yaml.org,2002:map>
       =VAL :k
       +SEQ [] <tag:yaml.org,2002:seq>
        =VAL :a
        =VAL <tag:yaml.org,2002:str> :b
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "k": [
        "a",
        "b"
      ]
    }
  dump: |
    !!map
    k: !!seq
    - a
    - !!str b
)RAW";

static constexpr std::string_view T_EW3V = R"RAW(
---
- name: Wrong indendation in mapping
  from: '@perlpunk'
  tags: error mapping indent
  fail: true
  yaml: |
    k1: v1
     k2: v2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :k1
)RAW";

static constexpr std::string_view T_EX5H = R"RAW(
---
- name: Multiline Scalar at Top Level [1.3]
  from: 9YRD, modified for YAML 1.3
  tags: scalar whitespace 1.3-mod
  yaml: |
    ---
    a
    b␣␣
      c
    d

    e
  tree: |
    +STR
     +DOC ---
      =VAL :a b c d\ne
     -DOC
    -STR
  json: |
    "a b c d\ne"
  dump: |
    'a b c d

      e'
  emit: |
    --- a b c d

    e
)RAW";

static constexpr std::string_view T_EXG3 = R"RAW(
---
- name: Three dashes and content without space [1.3]
  from: 82AN, modified for YAML 1.3
  tags: scalar 1.3-mod
  yaml: |
    ---
    ---word1
    word2
  tree: |
    +STR
     +DOC ---
      =VAL :---word1 word2
     -DOC
    -STR
  json: |
    "---word1 word2"
  dump: |
    '---word1 word2'
  emit: |
    --- '---word1 word2'
)RAW";

static constexpr std::string_view T_F2C7 = R"RAW(
---
- name: Anchors and Tags
  from: NimYAML tests
  tags: anchor tag
  yaml: |2
     - &a !!str a
     - !!int 2
     - !!int &c 4
     - &d d
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL &a <tag:yaml.org,2002:str> :a
       =VAL <tag:yaml.org,2002:int> :2
       =VAL &c <tag:yaml.org,2002:int> :4
       =VAL &d :d
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a",
      2,
      4,
      "d"
    ]
  dump: |
    - &a !!str a
    - !!int 2
    - &c !!int 4
    - &d d
)RAW";

static constexpr std::string_view T_F3CP = R"RAW(
---
- name: Nested flow collections on one line
  from: '@perlpunk'
  tags: flow mapping sequence
  yaml: |
    ---
    { a: [b, c, { d: [e, f] } ] }
  tree: |
    +STR
     +DOC ---
      +MAP {}
       =VAL :a
       +SEQ []
        =VAL :b
        =VAL :c
        +MAP {}
         =VAL :d
         +SEQ []
          =VAL :e
          =VAL :f
         -SEQ
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": [
        "b",
        "c",
        {
          "d": [
            "e",
            "f"
          ]
        }
      ]
    }
  dump: |
    ---
    a:
    - b
    - c
    - d:
      - e
      - f
)RAW";

static constexpr std::string_view T_F6MC = R"RAW(
---
- name: More indented lines at the beginning of folded block scalars
  from: '@perlpunk'
  tags: folded indent
  yaml: |
    ---
    a: >2
       more indented
      regular
    b: >2


       more indented
      regular
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
       =VAL > more indented\nregular\n
       =VAL :b
       =VAL >\n\n more indented\nregular\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": " more indented\nregular\n",
      "b": "\n\n more indented\nregular\n"
    }
  emit: |
    ---
    a: >2
       more indented
      regular
    b: >2


       more indented
      regular
)RAW";

static constexpr std::string_view T_F8F9 = R"RAW(
---
- name: Spec Example 8.5. Chomping Trailing Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2795435
  tags: spec literal scalar comment
  yaml: |2
     # Strip
      # Comments:
    strip: |-
      # text
    ␣␣
     # Clip
      # comments:

    clip: |
      # text
    ␣
     # Keep
      # comments:

    keep: |+
      # text

     # Trail
      # comments.
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :strip
       =VAL |# text
       =VAL :clip
       =VAL |# text\n
       =VAL :keep
       =VAL |# text\n\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "strip": "# text",
      "clip": "# text\n",
      "keep": "# text\n\n"
    }
  dump: |
    strip: |-
      # text
    clip: |
      # text
    keep: |+
      # text

    ...
)RAW";

static constexpr std::string_view T_FBC9 = R"RAW(
---
- name: Allowed characters in plain scalars
  from: '@perlpunk'
  tags: scalar
  yaml: |
    safe: a!"#$%&'()*+,-./09:;<=>?@AZ[\]^_`az{|}~
         !"#$%&'()*+,-./09:;<=>?@AZ[\]^_`az{|}~
    safe question mark: ?foo
    safe colon: :foo
    safe dash: -foo
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :safe
       =VAL :a!"#$%&'()*+,-./09:;<=>?@AZ[\\]^_`az{|}~ !"#$%&'()*+,-./09:;<=>?@AZ[\\]^_`az{|}~
       =VAL :safe question mark
       =VAL :?foo
       =VAL :safe colon
       =VAL ::foo
       =VAL :safe dash
       =VAL :-foo
      -MAP
     -DOC
    -STR
  json: |
    {
      "safe": "a!\"#$%&'()*+,-./09:;<=>?@AZ[\\]^_`az{|}~ !\"#$%&'()*+,-./09:;<=>?@AZ[\\]^_`az{|}~",
      "safe question mark": "?foo",
      "safe colon": ":foo",
      "safe dash": "-foo"
    }
  dump: |
    safe: a!"#$%&'()*+,-./09:;<=>?@AZ[\]^_`az{|}~ !"#$%&'()*+,-./09:;<=>?@AZ[\]^_`az{|}~
    safe question mark: ?foo
    safe colon: :foo
    safe dash: -foo
)RAW";

static constexpr std::string_view T_FH7J = R"RAW(
---
- name: Tags on Empty Scalars
  from: NimYAML tests
  tags: tag scalar
  yaml: |
    - !!str
    -
      !!null : a
      b: !!str
    - !!str : !!null
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL <tag:yaml.org,2002:str> :
       +MAP
        =VAL <tag:yaml.org,2002:null> :
        =VAL :a
        =VAL :b
        =VAL <tag:yaml.org,2002:str> :
       -MAP
       +MAP
        =VAL <tag:yaml.org,2002:str> :
        =VAL <tag:yaml.org,2002:null> :
       -MAP
      -SEQ
     -DOC
    -STR
  dump: |
    - !!str
    - !!null : a
      b: !!str
    - !!str : !!null
)RAW";

static constexpr std::string_view T_FP8R = R"RAW(
---
- name: Zero indented block scalar
  from: '@perlpunk'
  tags: folded indent scalar
  yaml: |
    --- >
    line1
    line2
    line3
  tree: |
    +STR
     +DOC ---
      =VAL >line1 line2 line3\n
     -DOC
    -STR
  json: |
    "line1 line2 line3\n"
  dump: |
    --- >
      line1 line2 line3
)RAW";

static constexpr std::string_view T_FQ7F = R"RAW(
---
- name: Spec Example 2.1. Sequence of Scalars
  from: http://www.yaml.org/spec/1.2/spec.html#id2759963
  tags: spec sequence
  yaml: |
    - Mark McGwire
    - Sammy Sosa
    - Ken Griffey
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :Mark McGwire
       =VAL :Sammy Sosa
       =VAL :Ken Griffey
      -SEQ
     -DOC
    -STR
  json: |
    [
      "Mark McGwire",
      "Sammy Sosa",
      "Ken Griffey"
    ]
  toke: |
    SEQ-MARK 0 1 1 1
    WS-SPACE 1 1 1 2
    TEXT-VAL 2 12 1 3 :Mark McGwire
    WS-NEWLN 14 1 1 15
    SEQ-MARK 15 1 2 1
    WS-SPACE 1 1 1 2
    TEXT-VAL 2 12 1 3 :Sammy Sosa
    WS-NEWLN 14 1 1 15
)RAW";

static constexpr std::string_view T_FRK4 = R"RAW(
---
- name: Spec Example 7.3. Completely Empty Flow Nodes
  from: http://www.yaml.org/spec/1.2/spec.html#id2786868
  tags: empty-key explicit-key spec flow mapping
  yaml: |
    {
      ? foo :,
      : bar,
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :foo
       =VAL :
       =VAL :
       =VAL :bar
      -MAP
     -DOC
    -STR
)RAW";

static constexpr std::string_view T_FTA2 = R"RAW(
---
- name: Single block sequence with anchor and explicit document start
  from: '@perlpunk'
  tags: anchor header sequence
  yaml: |
    --- &sequence
    - a
  tree: |
    +STR
     +DOC ---
      +SEQ &sequence
       =VAL :a
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a"
    ]
  dump: |
    --- &sequence
    - a
)RAW";

static constexpr std::string_view T_FUP4 = R"RAW(
---
- name: Flow Sequence in Flow Sequence
  from: NimYAML tests
  tags: sequence flow
  yaml: |
    [a, [b, c]]
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL :a
       +SEQ []
        =VAL :b
        =VAL :c
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a",
      [
        "b",
        "c"
      ]
    ]
  dump: |
    - a
    - - b
      - c
)RAW";

static constexpr std::string_view T_G4RS = R"RAW(
---
- name: Spec Example 2.17. Quoted Scalars
  from: http://www.yaml.org/spec/1.2/spec.html#id2761245
  tags: spec scalar
  yaml: |
    unicode: "Sosa did fine.\u263A"
    control: "\b1998\t1999\t2000\n"
    hex esc: "\x0d\x0a is \r\n"

    single: '"Howdy!" he cried.'
    quoted: ' # Not a ''comment''.'
    tie-fighter: '|\-*-/|'
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :unicode
       =VAL "Sosa did fine.☺
       =VAL :control
       =VAL "\b1998\t1999\t2000\n
       =VAL :hex esc
       =VAL "\r\n is \r\n
       =VAL :single
       =VAL '"Howdy!" he cried.
       =VAL :quoted
       =VAL ' # Not a 'comment'.
       =VAL :tie-fighter
       =VAL '|\\-*-/|
      -MAP
     -DOC
    -STR
  json: |
    {
      "unicode": "Sosa did fine.☺",
      "control": "\b1998\t1999\t2000\n",
      "hex esc": "\r\n is \r\n",
      "single": "\"Howdy!\" he cried.",
      "quoted": " # Not a 'comment'.",
      "tie-fighter": "|\\-*-/|"
    }
  dump: |
    unicode: "Sosa did fine.\u263A"
    control: "\b1998\t1999\t2000\n"
    hex esc: "\r\n is \r\n"
    single: '"Howdy!" he cried.'
    quoted: ' # Not a ''comment''.'
    tie-fighter: '|\-*-/|'
)RAW";

static constexpr std::string_view T_G5U8 = R"RAW(
---
- name: Plain dashes in flow sequence
  from: '@ingydotnet'
  tags: flow sequence
  fail: true
  yaml: |
    ---
    - [-, -]
  tree: |
    +STR
     +DOC ---
      +SEQ
       +SEQ []
)RAW";

static constexpr std::string_view T_G7JE = R"RAW(
---
- name: Multiline implicit keys
  from: '@perlpunk'
  tags: error mapping
  fail: true
  yaml: |
    a\nb: 1
    c
     d: 1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a\\nb
       =VAL :1
)RAW";

static constexpr std::string_view T_G992 = R"RAW(
---
- name: Spec Example 8.9. Folded Scalar
  from: http://www.yaml.org/spec/1.2/spec.html#id2796371
  tags: spec folded scalar 1.3-err
  yaml: |
    >
     folded
     text
    ↵
    ↵
  tree: |
    +STR
     +DOC
      =VAL >folded text\n
     -DOC
    -STR
  json: |
    "folded text\n"
  dump: |
    >
      folded text
)RAW";

static constexpr std::string_view T_G9HC = R"RAW(
---
- name: Invalid anchor in zero indented sequence
  from: '@perlpunk'
  tags: anchor error sequence
  fail: true
  yaml: |
    ---
    seq:
    &anchor
    - a
    - b
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :seq
)RAW";

static constexpr std::string_view T_GDY7 = R"RAW(
---
- name: Comment that looks like a mapping key
  from: '@perlpunk'
  tags: comment error mapping
  fail: true
  yaml: |
    key: value
    this is #not a: key
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL :value
)RAW";

static constexpr std::string_view T_GH63 = R"RAW(
---
- name: Mixed Block Mapping (explicit to implicit)
  from: NimYAML tests
  tags: explicit-key mapping
  yaml: |
    ? a
    : 1.3
    fifteen: d
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL :1.3
       =VAL :fifteen
       =VAL :d
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": 1.3,
      "fifteen": "d"
    }
  dump: |
    a: 1.3
    fifteen: d
)RAW";

static constexpr std::string_view T_GT5M = R"RAW(
---
- name: Node anchor in sequence
  from: '@perlpunk'
  tags: anchor error sequence
  fail: true
  yaml: |
    - item1
    &node
    - item2
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :item1
)RAW";

static constexpr std::string_view T_H2RW = R"RAW(
---
- name: Blank lines
  from: IRC discussion with leont
  tags: comment literal scalar whitespace
  yaml: |
    foo: 1

    bar: 2
    ␣␣␣␣
    text: |
      a
    ␣␣␣␣
      b

      c
    ␣
      d
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :1
       =VAL :bar
       =VAL :2
       =VAL :text
       =VAL |a\n  \nb\n\nc\n\nd\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": 1,
      "bar": 2,
      "text": "a\n  \nb\n\nc\n\nd\n"
    }
  dump: |
    foo: 1
    bar: 2
    text: "a\n  \nb\n\nc\n\nd\n"
  emit: |
    foo: 1
    bar: 2
    text: |
      a
    ␣␣␣␣
      b

      c

      d
)RAW";

static constexpr std::string_view T_H3Z8 = R"RAW(
---
- name: Literal unicode
  from: '@perlpunk'
  tags: scalar
  yaml: |
    ---
    wanted: love ♥ and peace ☮
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :wanted
       =VAL :love ♥ and peace ☮
      -MAP
     -DOC
    -STR
  json: |
    {
      "wanted": "love ♥ and peace ☮"
    }
  dump: |
    ---
    wanted: "love \u2665 and peace \u262E"
)RAW";

static constexpr std::string_view T_H7J7 = R"RAW(
---
- name: Node anchor not indented
  from: https://gist.github.com/anonymous/f192e7dab6da31831f264dbf1947cb83 via @ingydotnet
  tags: anchor error indent tag
  fail: true
  yaml: |
    key: &x
    !!map
      a: b
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL &x :
)RAW";

static constexpr std::string_view T_H7TQ = R"RAW(
---
- name: Extra words on %YAML directive
  from: '@ingydotnet'
  tags: directive
  fail: true
  yaml: |
    %YAML 1.2 foo
    ---
  tree: |
    +STR
)RAW";

static constexpr std::string_view T_HM87 = R"RAW(
---
- name: Scalars in flow start with syntax char
  from: '@ingydotnet'
  tags: flow scalar
  yaml: |
    [:x]
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL ::x
      -SEQ
     -DOC
    -STR
  json: |
    [
      ":x"
    ]
  dump: |
    - :x

- yaml: |
    [?x]
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL :?x
      -SEQ
     -DOC
    -STR
  json: |
    [
      "?x"
    ]
  dump: |
    - ?x
)RAW";

static constexpr std::string_view T_HMK4 = R"RAW(
---
- name: Spec Example 2.16. Indentation determines scope
  from: http://www.yaml.org/spec/1.2/spec.html#id2761083
  tags: spec folded literal
  yaml: |
    name: Mark McGwire
    accomplishment: >
      Mark set a major league
      home run record in 1998.
    stats: |
      65 Home Runs
      0.278 Batting Average
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :name
       =VAL :Mark McGwire
       =VAL :accomplishment
       =VAL >Mark set a major league home run record in 1998.\n
       =VAL :stats
       =VAL |65 Home Runs\n0.278 Batting Average\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "name": "Mark McGwire",
      "accomplishment": "Mark set a major league home run record in 1998.\n",
      "stats": "65 Home Runs\n0.278 Batting Average\n"
    }
  dump: |
    name: Mark McGwire
    accomplishment: >
      Mark set a major league home run record in 1998.
    stats: |
      65 Home Runs
      0.278 Batting Average
)RAW";

static constexpr std::string_view T_HMQ5 = R"RAW(
---
- name: Spec Example 6.23. Node Properties
  from: http://www.yaml.org/spec/1.2/spec.html#id2783940
  tags: spec tag alias
  yaml: |
    !!str &a1 "foo":
      !!str bar
    &a2 baz : *a1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL &a1 <tag:yaml.org,2002:str> "foo
       =VAL <tag:yaml.org,2002:str> :bar
       =VAL &a2 :baz
       =ALI *a1
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "bar",
      "baz": "foo"
    }
  dump: |
    &a1 !!str "foo": !!str bar
    &a2 baz: *a1
)RAW";

static constexpr std::string_view T_HRE5 = R"RAW(
---
- name: Double quoted scalar with escaped single quote
  from: https://github.com/yaml/libyaml/issues/68
  tags: double error single
  fail: true
  yaml: |
    ---
    double: "quoted \' scalar"
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :double
)RAW";

static constexpr std::string_view T_HS5T = R"RAW(
---
- name: Spec Example 7.12. Plain Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2789986
  tags: spec scalar whitespace upto-1.2
  yaml: |
    1st non-empty

     2nd non-empty␣
    ———»3rd non-empty
  tree: |
    +STR
     +DOC
      =VAL :1st non-empty\n2nd non-empty 3rd non-empty
     -DOC
    -STR
  json: |
    "1st non-empty\n2nd non-empty 3rd non-empty"
  dump: |
    '1st non-empty

      2nd non-empty 3rd non-empty'
)RAW";

static constexpr std::string_view T_HU3P = R"RAW(
---
- name: Invalid Mapping in plain scalar
  from: https://gist.github.com/anonymous/d305fd8e54cfe7a484088c91a8a2e533 via @ingydotnet
  tags: error mapping scalar
  fail: true
  yaml: |
    key:
      word1 word2
      no: key
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
)RAW";

static constexpr std::string_view T_HWV9 = R"RAW(
---
- name: Document-end marker
  from: '@perlpunk'
  tags: footer
  yaml: |
    ...
  tree: |
    +STR
    -STR
  json: ''
  dump: ''
)RAW";

static constexpr std::string_view T_J3BT = R"RAW(
---
- name: Spec Example 5.12. Tabs and Spaces
  from: http://www.yaml.org/spec/1.2/spec.html#id2775350
  tags: spec whitespace upto-1.2
  yaml: |
    # Tabs and spaces
    quoted: "Quoted ———»"
    block:—»|
      void main() {
      —»printf("Hello, world!\n");
      }
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :quoted
       =VAL "Quoted \t
       =VAL :block
       =VAL |void main() {\n\tprintf("Hello, world!\\n");\n}\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "quoted": "Quoted \t",
      "block": "void main() {\n\tprintf(\"Hello, world!\\n\");\n}\n"
    }
  dump: |
    quoted: "Quoted \t"
    block: |
      void main() {
      —»printf("Hello, world!\n");
      }
)RAW";

static constexpr std::string_view T_J5UC = R"RAW(
---
- name: Multiple Pair Block Mapping
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/mapping.tml
  tags: mapping
  yaml: |
    foo: blue
    bar: arrr
    baz: jazz
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL :blue
       =VAL :bar
       =VAL :arrr
       =VAL :baz
       =VAL :jazz
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "blue",
      "bar": "arrr",
      "baz": "jazz"
    }
)RAW";

static constexpr std::string_view T_J7PZ = R"RAW(
---
- name: Spec Example 2.26. Ordered Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2761780
  tags: spec mapping tag unknown-tag
  yaml: |
    # The !!omap tag is one of the optional types
    # introduced for YAML 1.1. In 1.2, it is not
    # part of the standard tags and should not be
    # enabled by default.
    # Ordered maps are represented as
    # A sequence of mappings, with
    # each mapping having one key
    --- !!omap
    - Mark McGwire: 65
    - Sammy Sosa: 63
    - Ken Griffy: 58
  tree: |
    +STR
     +DOC ---
      +SEQ <tag:yaml.org,2002:omap>
       +MAP
        =VAL :Mark McGwire
        =VAL :65
       -MAP
       +MAP
        =VAL :Sammy Sosa
        =VAL :63
       -MAP
       +MAP
        =VAL :Ken Griffy
        =VAL :58
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "Mark McGwire": 65
      },
      {
        "Sammy Sosa": 63
      },
      {
        "Ken Griffy": 58
      }
    ]
  dump: |
    --- !!omap
    - Mark McGwire: 65
    - Sammy Sosa: 63
    - Ken Griffy: 58
)RAW";

static constexpr std::string_view T_J7VC = R"RAW(
---
- name: Empty Lines Between Mapping Elements
  from: NimYAML tests
  tags: whitespace mapping
  yaml: |
    one: 2


    three: 4
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :one
       =VAL :2
       =VAL :three
       =VAL :4
      -MAP
     -DOC
    -STR
  json: |
    {
      "one": 2,
      "three": 4
    }
  dump: |
    one: 2
    three: 4
)RAW";

static constexpr std::string_view T_J9HZ = R"RAW(
---
- name: Spec Example 2.9. Single Document with Two Comments
  from: http://www.yaml.org/spec/1.2/spec.html#id2760633
  tags: mapping sequence spec comment
  yaml: |
    ---
    hr: # 1998 hr ranking
      - Mark McGwire
      - Sammy Sosa
    rbi:
      # 1998 rbi ranking
      - Sammy Sosa
      - Ken Griffey
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :hr
       +SEQ
        =VAL :Mark McGwire
        =VAL :Sammy Sosa
       -SEQ
       =VAL :rbi
       +SEQ
        =VAL :Sammy Sosa
        =VAL :Ken Griffey
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "hr": [
        "Mark McGwire",
        "Sammy Sosa"
      ],
      "rbi": [
        "Sammy Sosa",
        "Ken Griffey"
      ]
    }
  dump: |
    ---
    hr:
    - Mark McGwire
    - Sammy Sosa
    rbi:
    - Sammy Sosa
    - Ken Griffey
)RAW";

static constexpr std::string_view T_JEF9 = R"RAW(
---
- name: Trailing whitespace in streams
  from: '@ingydotnet'
  tags: literal
  yaml: |
    - |+
    ↵
    ↵
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |\n\n
      -SEQ
     -DOC
    -STR
  json: |
    [
      "\n\n"
    ]
  dump: |
    - |+
    ↵
    ↵
    ...

- yaml: |
    - |+
    ␣␣␣
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |\n
      -SEQ
     -DOC
    -STR
  json: |
    [
      "\n"
    ]
  dump: |
    - |+

    ...

- yaml: |
    - |+
    ␣␣␣∎
  dump: |
    - |+

    ...
)RAW";

static constexpr std::string_view T_JHB9 = R"RAW(
---
- name: Spec Example 2.7. Two Documents in a Stream
  from: http://www.yaml.org/spec/1.2/spec.html#id2760493
  tags: spec header
  yaml: |
    # Ranking of 1998 home runs
    ---
    - Mark McGwire
    - Sammy Sosa
    - Ken Griffey

    # Team ranking
    ---
    - Chicago Cubs
    - St Louis Cardinals
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL :Mark McGwire
       =VAL :Sammy Sosa
       =VAL :Ken Griffey
      -SEQ
     -DOC
     +DOC ---
      +SEQ
       =VAL :Chicago Cubs
       =VAL :St Louis Cardinals
      -SEQ
     -DOC
    -STR
  json: |
    [
      "Mark McGwire",
      "Sammy Sosa",
      "Ken Griffey"
    ]
    [
      "Chicago Cubs",
      "St Louis Cardinals"
    ]
  dump: |
    ---
    - Mark McGwire
    - Sammy Sosa
    - Ken Griffey
    ---
    - Chicago Cubs
    - St Louis Cardinals
)RAW";

static constexpr std::string_view T_JKF3 = R"RAW(
---
- name: Multiline unidented double quoted block key
  from: '@ingydotnet'
  tags: indent
  fail: true
  yaml: |
    - - "bar
    bar": x
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ
)RAW";

static constexpr std::string_view T_JQ4R = R"RAW(
---
- name: Spec Example 8.14. Block Sequence
  from: http://www.yaml.org/spec/1.2/spec.html#id2797596
  tags: mapping spec sequence
  yaml: |
    block sequence:
      - one
      - two : three
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :block sequence
       +SEQ
        =VAL :one
        +MAP
         =VAL :two
         =VAL :three
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "block sequence": [
        "one",
        {
          "two": "three"
        }
      ]
    }
  dump: |
    block sequence:
    - one
    - two: three
)RAW";

static constexpr std::string_view T_JR7V = R"RAW(
---
- name: Question marks in scalars
  from: '@perlpunk'
  tags: flow scalar
  yaml: |
    - a?string
    - another ? string
    - key: value?
    - [a?string]
    - [another ? string]
    - {key: value? }
    - {key: value?}
    - {key?: value }
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :a?string
       =VAL :another ? string
       +MAP
        =VAL :key
        =VAL :value?
       -MAP
       +SEQ []
        =VAL :a?string
       -SEQ
       +SEQ []
        =VAL :another ? string
       -SEQ
       +MAP {}
        =VAL :key
        =VAL :value?
       -MAP
       +MAP {}
        =VAL :key
        =VAL :value?
       -MAP
       +MAP {}
        =VAL :key?
        =VAL :value
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a?string",
      "another ? string",
      {
        "key": "value?"
      },
      [
        "a?string"
      ],
      [
        "another ? string"
      ],
      {
        "key": "value?"
      },
      {
        "key": "value?"
      },
      {
        "key?": "value"
      }
    ]
  dump: |
    - a?string
    - another ? string
    - key: value?
    - - a?string
    - - another ? string
    - key: value?
    - key: value?
    - key?: value
)RAW";

static constexpr std::string_view T_JS2J = R"RAW(
---
- name: Spec Example 6.29. Node Anchors
  from: http://www.yaml.org/spec/1.2/spec.html#id2785977
  tags: spec alias
  yaml: |
    First occurrence: &anchor Value
    Second occurrence: *anchor
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :First occurrence
       =VAL &anchor :Value
       =VAL :Second occurrence
       =ALI *anchor
      -MAP
     -DOC
    -STR
  json: |
    {
      "First occurrence": "Value",
      "Second occurrence": "Value"
    }
)RAW";

static constexpr std::string_view T_JTV5 = R"RAW(
---
- name: Block Mapping with Multiline Scalars
  from: NimYAML tests
  tags: explicit-key mapping scalar
  yaml: |
    ? a
      true
    : null
      d
    ? e
      42
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a true
       =VAL :null d
       =VAL :e 42
       =VAL :
      -MAP
     -DOC
    -STR
  json: |
    {
      "a true": "null d",
      "e 42": null
    }
  dump: |
    a true: null d
    e 42:
)RAW";

static constexpr std::string_view T_JY7Z = R"RAW(
---
- name: Trailing content that looks like a mapping
  from: '@perlpunk'
  tags: error mapping double
  fail: true
  yaml: |
    key1: "quoted1"
    key2: "quoted2" no key: nor value
    key3: "quoted3"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key1
       =VAL "quoted1
       =VAL :key2
       =VAL "quoted2
)RAW";

static constexpr std::string_view T_K3WX = R"RAW(
---
- name: Colon and adjacent value after comment on next line
  from: <Source URL or description>
  tags: comment flow mapping
  yaml: |
    ---
    { "foo" # comment
      :bar }
  tree: |
    +STR
     +DOC ---
      +MAP {}
       =VAL "foo
       =VAL :bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "bar"
    }
  dump: |
    ---
    "foo": bar
)RAW";

static constexpr std::string_view T_K4SU = R"RAW(
---
- name: Multiple Entry Block Sequence
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/sequence.tml
  tags: sequence
  yaml: |
    - foo
    - bar
    - 42
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :foo
       =VAL :bar
       =VAL :42
      -SEQ
     -DOC
    -STR
  json: |
    [
      "foo",
      "bar",
      42
    ]
)RAW";

static constexpr std::string_view T_K527 = R"RAW(
---
- name: Spec Example 6.6. Line Folding
  from: http://www.yaml.org/spec/1.2/spec.html#id2779289
  tags: folded spec whitespace scalar 1.3-err
  yaml: |
    >-
      trimmed
    ␣␣
    ␣

      as
      space
  tree: |
    +STR
     +DOC
      =VAL >trimmed\n\n\nas space
     -DOC
    -STR
  json: |
    "trimmed\n\n\nas space"
  dump: |
    >-
      trimmed



      as space
)RAW";

static constexpr std::string_view T_K54U = R"RAW(
---
- name: Tab after document header
  from: '@perlpunk'
  tags: header whitespace
  yaml: |
    ---»scalar
  tree: |
    +STR
     +DOC ---
      =VAL :scalar
     -DOC
    -STR
  json: |
    "scalar"
  dump: |
    --- scalar
    ...
)RAW";

static constexpr std::string_view T_K858 = R"RAW(
---
- name: Spec Example 8.6. Empty Scalar Chomping
  from: http://www.yaml.org/spec/1.2/spec.html#id2795596
  tags: spec folded literal whitespace
  yaml: |
    strip: >-

    clip: >

    keep: |+
    ↵
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :strip
       =VAL >
       =VAL :clip
       =VAL >
       =VAL :keep
       =VAL |\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "strip": "",
      "clip": "",
      "keep": "\n"
    }
  dump: |
    strip: ""
    clip: ""
    keep: |2+

    ...
)RAW";

static constexpr std::string_view T_KH5V = R"RAW(
---
- name: Inline tabs in double quoted
  from: '@ingydotnet'
  tags: double whitespace
  yaml: |
    "1 inline\ttab"
  tree: |
    +STR
     +DOC
      =VAL "1 inline\ttab
     -DOC
    -STR
  json: |
    "1 inline\ttab"

- yaml: |
    "2 inline\——»tab"
  tree: |
    +STR
     +DOC
      =VAL "2 inline\ttab
     -DOC
    -STR
  json: |
    "2 inline\ttab"
  dump: |
    "2 inline\ttab"

- yaml: |
    "3 inline———»tab"
  tree: |
    +STR
     +DOC
      =VAL "3 inline\ttab
     -DOC
    -STR
  json: |
    "3 inline\ttab"
  dump: |
    "3 inline\ttab"
)RAW";

static constexpr std::string_view T_KK5P = R"RAW(
---
- name: Various combinations of explicit block mappings
  from: '@perlpunk'
  tags: explicit-key mapping sequence
  yaml: |
    complex1:
      ? - a
    complex2:
      ? - a
      : b
    complex3:
      ? - a
      : >
        b
    complex4:
      ? >
        a
      :
    complex5:
      ? - a
      : - b
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :complex1
       +MAP
        +SEQ
         =VAL :a
        -SEQ
        =VAL :
       -MAP
       =VAL :complex2
       +MAP
        +SEQ
         =VAL :a
        -SEQ
        =VAL :b
       -MAP
       =VAL :complex3
       +MAP
        +SEQ
         =VAL :a
        -SEQ
        =VAL >b\n
       -MAP
       =VAL :complex4
       +MAP
        =VAL >a\n
        =VAL :
       -MAP
       =VAL :complex5
       +MAP
        +SEQ
         =VAL :a
        -SEQ
        +SEQ
         =VAL :b
        -SEQ
       -MAP
      -MAP
     -DOC
    -STR
  dump: |
    complex1:
      ? - a
      :
    complex2:
      ? - a
      : b
    complex3:
      ? - a
      : >
        b
    complex4:
      ? >
        a
      :
    complex5:
      ? - a
      : - b
)RAW";

static constexpr std::string_view T_KMK3 = R"RAW(
---
- name: Block Submapping
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/mapping.tml
  tags: mapping
  yaml: |
    foo:
      bar: 1
    baz: 2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       +MAP
        =VAL :bar
        =VAL :1
       -MAP
       =VAL :baz
       =VAL :2
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": {
        "bar": 1
      },
      "baz": 2
    }
)RAW";

static constexpr std::string_view T_KS4U = R"RAW(
---
- name: Invalid item after end of flow sequence
  from: '@perlpunk'
  tags: error flow sequence
  fail: true
  yaml: |
    ---
    [
    sequence item
    ]
    invalid item
  tree: |
    +STR
     +DOC ---
      +SEQ []
       =VAL :sequence item
      -SEQ
)RAW";

static constexpr std::string_view T_KSS4 = R"RAW(
---
- name: Scalars on --- line
  from: '@perlpunk'
  tags: anchor header scalar 1.3-err
  yaml: |
    --- "quoted
    string"
    --- &node foo
  tree: |
    +STR
     +DOC ---
      =VAL "quoted string
     -DOC
     +DOC ---
      =VAL &node :foo
     -DOC
    -STR
  json: |
    "quoted string"
    "foo"
  dump: |
    --- "quoted string"
    --- &node foo
    ...
  emit: |
    --- "quoted string"
    --- &node foo
)RAW";

static constexpr std::string_view T_L24T = R"RAW(
---
- name: Trailing line of spaces
  from: '@ingydotnet'
  tags: whitespace
  yaml: |
    foo: |
      x
    ␣␣␣
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL |x\n \n
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : "x\n \n"
    }
  emit: |
    ---
    foo: "x\n \n"

- yaml: |
    foo: |
      x
    ␣␣␣∎
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL |x\n \n
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo" : "x\n \n"
    }
  emit: |
    ---
    foo: "x\n \n"
)RAW";

static constexpr std::string_view T_L383 = R"RAW(
---
- name: Two scalar docs with trailing comments
  from: '@ingydotnet'
  tags: comment
  yaml: |
    --- foo  # comment
    --- foo  # comment
  tree: |
    +STR
     +DOC ---
      =VAL :foo
     -DOC
     +DOC ---
      =VAL :foo
     -DOC
    -STR
  json: |
    "foo"
    "foo"
  dump: |
    --- foo
    --- foo
)RAW";

static constexpr std::string_view T_L94M = R"RAW(
---
- name: Tags in Explicit Mapping
  from: NimYAML tests
  tags: explicit-key tag mapping
  yaml: |
    ? !!str a
    : !!int 47
    ? c
    : !!str d
  tree: |
    +STR
     +DOC
      +MAP
       =VAL <tag:yaml.org,2002:str> :a
       =VAL <tag:yaml.org,2002:int> :47
       =VAL :c
       =VAL <tag:yaml.org,2002:str> :d
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": 47,
      "c": "d"
    }
  dump: |
    !!str a: !!int 47
    c: !!str d
)RAW";

static constexpr std::string_view T_L9U5 = R"RAW(
---
- name: Spec Example 7.11. Plain Implicit Keys
  from: http://www.yaml.org/spec/1.2/spec.html#id2789794
  tags: spec flow mapping
  yaml: |
    implicit block key : [
      implicit flow key : value,
     ]
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :implicit block key
       +SEQ []
        +MAP {}
         =VAL :implicit flow key
         =VAL :value
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "implicit block key": [
        {
          "implicit flow key": "value"
        }
      ]
    }
  dump: |
    implicit block key:
    - implicit flow key: value
)RAW";

static constexpr std::string_view T_LE5A = R"RAW(
---
- name: Spec Example 7.24. Flow Nodes
  from: http://www.yaml.org/spec/1.2/spec.html#id2793490
  tags: spec tag alias
  yaml: |
    - !!str "a"
    - 'b'
    - &anchor "c"
    - *anchor
    - !!str
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL <tag:yaml.org,2002:str> "a
       =VAL 'b
       =VAL &anchor "c
       =ALI *anchor
       =VAL <tag:yaml.org,2002:str> :
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a",
      "b",
      "c",
      "c",
      ""
    ]
)RAW";

static constexpr std::string_view T_LHL4 = R"RAW(
---
- name: Invalid tag
  from: '@perlpunk'
  tags: error tag
  fail: true
  yaml: |
    ---
    !invalid{}tag scalar
  tree: |
    +STR
     +DOC ---
)RAW";

static constexpr std::string_view T_LP6E = R"RAW(
---
- name: Whitespace After Scalars in Flow
  from: NimYAML tests
  tags: flow scalar whitespace
  yaml: |
    - [a, b , c ]
    - { "a"  : b
       , c : 'd' ,
       e   : "f"
      }
    - [      ]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        =VAL :a
        =VAL :b
        =VAL :c
       -SEQ
       +MAP {}
        =VAL "a
        =VAL :b
        =VAL :c
        =VAL 'd
        =VAL :e
        =VAL "f
       -MAP
       +SEQ []
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      [
        "a",
        "b",
        "c"
      ],
      {
        "a": "b",
        "c": "d",
        "e": "f"
      },
      []
    ]
  dump: |
    - - a
      - b
      - c
    - "a": b
      c: 'd'
      e: "f"
    - []
)RAW";

static constexpr std::string_view T_LQZ7 = R"RAW(
---
- name: Spec Example 7.4. Double Quoted Implicit Keys
  from: http://www.yaml.org/spec/1.2/spec.html#id2787420
  tags: spec scalar flow
  yaml: |
    "implicit block key" : [
      "implicit flow key" : value,
     ]
  tree: |
    +STR
     +DOC
      +MAP
       =VAL "implicit block key
       +SEQ []
        +MAP {}
         =VAL "implicit flow key
         =VAL :value
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "implicit block key": [
        {
          "implicit flow key": "value"
        }
      ]
    }
  dump: |
    "implicit block key":
    - "implicit flow key": value
)RAW";

static constexpr std::string_view T_LX3P = R"RAW(
---
- name: Implicit Flow Mapping Key on one line
  from: '@perlpunk'
  tags: complex-key mapping flow sequence 1.3-err
  yaml: |
    [flow]: block
  tree: |
    +STR
     +DOC
      +MAP
       +SEQ []
        =VAL :flow
       -SEQ
       =VAL :block
      -MAP
     -DOC
    -STR
  dump: |
    ? - flow
    : block
)RAW";

static constexpr std::string_view T_M29M = R"RAW(
---
- name: Literal Block Scalar
  from: NimYAML tests
  tags: literal scalar whitespace
  yaml: |
    a: |
     ab
    ␣
     cd
     ef
    ␣

    ...
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL |ab\n\ncd\nef\n
      -MAP
     -DOC ...
    -STR
  json: |
    {
      "a": "ab\n\ncd\nef\n"
    }
  dump: |
    a: |
      ab

      cd
      ef
    ...
)RAW";

static constexpr std::string_view T_M2N8 = R"RAW(
---
- name: Question mark edge cases
  from: '@ingydotnet'
  tags: edge empty-key
  yaml: |
    - ? : x
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        +MAP
         =VAL :
         =VAL :x
        -MAP
        =VAL :
       -MAP
      -SEQ
     -DOC
    -STR
  dump: |
    - ? : x
      :

- yaml: |
    ? []: x
  tree: |
    +STR
     +DOC
      +MAP
       +MAP
        +SEQ []
        -SEQ
        =VAL :x
       -MAP
       =VAL :
      -MAP
     -DOC
    -STR
  dump: |
    ? []: x
    :
)RAW";

static constexpr std::string_view T_M5C3 = R"RAW(
---
- name: Spec Example 8.21. Block Scalar Nodes
  from: http://www.yaml.org/spec/1.2/spec.html#id2799693
  tags: indent spec literal folded tag local-tag 1.3-err
  yaml: |
    literal: |2
      value
    folded:
       !foo
      >1
     value
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :literal
       =VAL |value\n
       =VAL :folded
       =VAL <!foo> >value\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "literal": "value\n",
      "folded": "value\n"
    }
  dump: |
    literal: |
      value
    folded: !foo >
      value
)RAW";

static constexpr std::string_view T_M5DY = R"RAW(
---
- name: Spec Example 2.11. Mapping between Sequences
  from: http://www.yaml.org/spec/1.2/spec.html#id2760799
  tags: complex-key explicit-key spec mapping sequence
  yaml: |
    ? - Detroit Tigers
      - Chicago cubs
    :
      - 2001-07-23

    ? [ New York Yankees,
        Atlanta Braves ]
    : [ 2001-07-02, 2001-08-12,
        2001-08-14 ]
  tree: |
    +STR
     +DOC
      +MAP
       +SEQ
        =VAL :Detroit Tigers
        =VAL :Chicago cubs
       -SEQ
       +SEQ
        =VAL :2001-07-23
       -SEQ
       +SEQ []
        =VAL :New York Yankees
        =VAL :Atlanta Braves
       -SEQ
       +SEQ []
        =VAL :2001-07-02
        =VAL :2001-08-12
        =VAL :2001-08-14
       -SEQ
      -MAP
     -DOC
    -STR
  dump: |
    ? - Detroit Tigers
      - Chicago cubs
    : - 2001-07-23
    ? - New York Yankees
      - Atlanta Braves
    : - 2001-07-02
      - 2001-08-12
      - 2001-08-14
)RAW";

static constexpr std::string_view T_M6YH = R"RAW(
---
- name: Block sequence indentation
  from: '@ingydotnet'
  tags: indent
  yaml: |
    - |
     x
    -
     foo: bar
    -
     - 42
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |x\n
       +MAP
        =VAL :foo
        =VAL :bar
       -MAP
       +SEQ
        =VAL :42
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      "x\n",
      {
        "foo" : "bar"
      },
      [
        42
      ]
    ]
  dump: |
    - |
      x
    - foo: bar
    - - 42
)RAW";

static constexpr std::string_view T_M7A3 = R"RAW(
---
- name: Spec Example 9.3. Bare Documents
  from: http://www.yaml.org/spec/1.2/spec.html#id2801226
  tags: spec footer 1.3-err
  yaml: |
    Bare
    document
    ...
    # No document
    ...
    |
    %!PS-Adobe-2.0 # Not the first line
  tree: |
    +STR
     +DOC
      =VAL :Bare document
     -DOC ...
     +DOC
      =VAL |%!PS-Adobe-2.0 # Not the first line\n
     -DOC
    -STR
  json: |
    "Bare document"
    "%!PS-Adobe-2.0 # Not the first line\n"
  emit: |
    Bare document
    ...
    |
      %!PS-Adobe-2.0 # Not the first line
)RAW";

static constexpr std::string_view T_M7NX = R"RAW(
---
- name: Nested flow collections
  from: '@perlpunk'
  tags: flow mapping sequence
  yaml: |
    ---
    {
     a: [
      b, c, {
       d: [e, f]
      }
     ]
    }
  tree: |
    +STR
     +DOC ---
      +MAP {}
       =VAL :a
       +SEQ []
        =VAL :b
        =VAL :c
        +MAP {}
         =VAL :d
         +SEQ []
          =VAL :e
          =VAL :f
         -SEQ
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": [
        "b",
        "c",
        {
          "d": [
            "e",
            "f"
          ]
        }
      ]
    }
  dump: |
    ---
    a:
    - b
    - c
    - d:
      - e
      - f
)RAW";

static constexpr std::string_view T_M9B4 = R"RAW(
---
- name: Spec Example 8.7. Literal Scalar
  from: http://www.yaml.org/spec/1.2/spec.html#id2795789
  tags: spec literal scalar whitespace 1.3-err
  yaml: |
    |
     literal
     ——»text
    ↵
    ↵
  tree: |
    +STR
     +DOC
      =VAL |literal\n\ttext\n
     -DOC
    -STR
  json: |
    "literal\n\ttext\n"
  dump: |
    |
      literal
      —»text
)RAW";

static constexpr std::string_view T_MJS9 = R"RAW(
---
- name: Spec Example 6.7. Block Folding
  from: http://www.yaml.org/spec/1.2/spec.html#id2779603
  tags: folded spec scalar whitespace 1.3-err
  yaml: |
    >
      foo␣
    ␣
      —» bar

      baz
  tree: |
    +STR
     +DOC
      =VAL >foo \n\n\t bar\n\nbaz\n
     -DOC
    -STR
  json: |
    "foo \n\n\t bar\n\nbaz\n"
  dump: |
    "foo \n\n\t bar\n\nbaz\n"
)RAW";

static constexpr std::string_view T_MUS6 = R"RAW(
- name: Directive variants
  from: '@ingydotnet'
  tags: directive
  also: ZYU8
  fail: true
  yaml: |
    %YAML 1.1#...
    ---
  tree: |
    +STR

- fail: true
  yaml: |
    %YAML 1.2
    ---
    %YAML 1.2
    ---
  dump: null

- yaml: |
    %YAML  1.1
    ---
  tree: |
    +STR
     +DOC ---
      =VAL :
     -DOC
    -STR
  json: |
    null
  dump: |
    ---

- yaml: |
    %YAML ——» 1.1
    ---

- yaml: |
    %YAML 1.1  # comment
    ---

- note: These 2 are reserved directives
  yaml: |
    %YAM 1.1
    ---

- yaml: |
    %YAMLL 1.1
    ---
)RAW";

static constexpr std::string_view T_MXS3 = R"RAW(
---
- name: Flow Mapping in Block Sequence
  from: NimYAML tests
  tags: mapping sequence flow
  yaml: |
    - {a: b}
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP {}
        =VAL :a
        =VAL :b
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "a": "b"
      }
    ]
  dump: |
    - a: b
)RAW";

static constexpr std::string_view T_MYW6 = R"RAW(
---
- name: Block Scalar Strip
  from: NimYAML tests
  tags: literal scalar whitespace 1.3-err
  yaml: |
    |-
     ab
    ␣
    ␣
    ...
  tree: |
    +STR
     +DOC
      =VAL |ab
     -DOC ...
    -STR
  json: |
    "ab"
  dump: |
    |-
      ab
    ...
)RAW";

static constexpr std::string_view T_MZX3 = R"RAW(
---
- name: Non-Specific Tags on Scalars
  from: NimYAML tests
  tags: folded scalar
  yaml: |
    - plain
    - "double quoted"
    - 'single quoted'
    - >
      block
    - plain again
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :plain
       =VAL "double quoted
       =VAL 'single quoted
       =VAL >block\n
       =VAL :plain again
      -SEQ
     -DOC
    -STR
  json: |
    [
      "plain",
      "double quoted",
      "single quoted",
      "block\n",
      "plain again"
    ]
)RAW";

static constexpr std::string_view T_N4JP = R"RAW(
---
- name: Bad indentation in mapping
  from: '@perlpunk'
  tags: error mapping indent double
  fail: true
  yaml: |
    map:
      key1: "quoted1"
     key2: "bad indentation"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :map
       +MAP
        =VAL :key1
        =VAL "quoted1
       -MAP
)RAW";

static constexpr std::string_view T_N782 = R"RAW(
---
- name: Invalid document markers in flow style
  from: NimYAML tests
  tags: flow edge header footer error
  fail: true
  yaml: |
    [
    --- ,
    ...
    ]
  tree: |
    +STR
     +DOC
      +SEQ []
)RAW";

static constexpr std::string_view T_NAT4 = R"RAW(
---
- name: Various empty or newline only quoted strings
  from: '@perlpunk'
  tags: double scalar single whitespace
  yaml: |
    ---
    a: '
      '
    b: '␣␣
      '
    c: "
      "
    d: "␣␣
      "
    e: '

      '
    f: "

      "
    g: '


      '
    h: "


      "
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
       =VAL '␣
       =VAL :b
       =VAL '␣
       =VAL :c
       =VAL "␣
       =VAL :d
       =VAL "␣
       =VAL :e
       =VAL '\n
       =VAL :f
       =VAL "\n
       =VAL :g
       =VAL '\n\n
       =VAL :h
       =VAL "\n\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": " ",
      "b": " ",
      "c": " ",
      "d": " ",
      "e": "\n",
      "f": "\n",
      "g": "\n\n",
      "h": "\n\n"
    }
  emit: |
    ---
    a: ' '
    b: ' '
    c: " "
    d: " "
    e: '

      '
    f: "\n"
    g: '


      '
    h: "\n\n"
)RAW";

static constexpr std::string_view T_NB6Z = R"RAW(
---
- name: Multiline plain value with tabs on empty lines
  from: '@perlpunk'
  tags: scalar whitespace
  yaml: |
    key:
      value
      with
      —»
      tabs
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL :value with\ntabs
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": "value with\ntabs"
    }
  dump: |
    key: 'value with

      tabs'
)RAW";

static constexpr std::string_view T_NHX8 = R"RAW(
---
- name: Empty Lines at End of Document
  from: NimYAML tests
  tags: empty-key whitespace
  yaml: |
    :
    ↵
    ↵
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :
       =VAL :
      -MAP
     -DOC
    -STR
  emit: |
    :
)RAW";

static constexpr std::string_view T_NJ66 = R"RAW(
---
- name: Multiline plain flow mapping key
  from: '@perlpunk'
  tags: flow mapping
  yaml: |
    ---
    - { single line: value}
    - { multi
      line: value}
  tree: |
    +STR
     +DOC ---
      +SEQ
       +MAP {}
        =VAL :single line
        =VAL :value
       -MAP
       +MAP {}
        =VAL :multi line
        =VAL :value
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "single line": "value"
      },
      {
        "multi line": "value"
      }
    ]
  dump: |
    ---
    - single line: value
    - multi line: value
)RAW";

static constexpr std::string_view T_NKF9 = R"RAW(
---
- name: Empty keys in block and flow mapping
  from: '@perlpunk'
  tags: empty-key mapping
  yaml: |
    ---
    key: value
    : empty key
    ---
    {
     key: value, : empty key
    }
    ---
    # empty key and value
    :
    ---
    # empty key and value
    { : }
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key
       =VAL :value
       =VAL :
       =VAL :empty key
      -MAP
     -DOC
     +DOC ---
      +MAP {}
       =VAL :key
       =VAL :value
       =VAL :
       =VAL :empty key
      -MAP
     -DOC
     +DOC ---
      +MAP
       =VAL :
       =VAL :
      -MAP
     -DOC
     +DOC ---
      +MAP {}
       =VAL :
       =VAL :
      -MAP
     -DOC
    -STR
  emit: |
    ---
    key: value
    : empty key
    ---
    key: value
    : empty key
    ---
    :
    ---
    :
)RAW";

static constexpr std::string_view T_NP9H = R"RAW(
---
- name: Spec Example 7.5. Double Quoted Line Breaks
  from: http://www.yaml.org/spec/1.2/spec.html#id2787745
  tags: double spec scalar whitespace upto-1.2
  yaml: |
    "folded␣
    to a space,»
    ␣
    to a line feed, or »\
     \ »non-content"
  tree: |
    +STR
     +DOC
      =VAL "folded to a space,\nto a line feed, or \t \tnon-content
     -DOC
    -STR
  json: |
    "folded to a space,\nto a line feed, or \t \tnon-content"
  dump: |
    "folded to a space,\nto a line feed, or \t \tnon-content"
)RAW";

static constexpr std::string_view T_P2AD = R"RAW(
---
- name: Spec Example 8.1. Block Scalar Header
  from: http://www.yaml.org/spec/1.2/spec.html#id2793888
  tags: spec literal folded comment scalar
  yaml: |
    - | # Empty header↓
     literal
    - >1 # Indentation indicator↓
      folded
    - |+ # Chomping indicator↓
     keep

    - >1- # Both indicators↓
      strip
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |literal\n
       =VAL > folded\n
       =VAL |keep\n\n
       =VAL > strip
      -SEQ
     -DOC
    -STR
  json: |
    [
      "literal\n",
      " folded\n",
      "keep\n\n",
      " strip"
    ]
  dump: |
    - |
      literal
    - >2
       folded
    - |+
      keep

    - >2-
       strip
)RAW";

static constexpr std::string_view T_P2EQ = R"RAW(
---
- name: Invalid sequene item on same line as previous item
  from: '@perlpunk'
  tags: error flow mapping sequence
  fail: true
  yaml: |
    ---
    - { y: z }- invalid
  tree: |
    +STR
     +DOC ---
      +SEQ
       +MAP {}
        =VAL :y
        =VAL :z
       -MAP
)RAW";

static constexpr std::string_view T_P76L = R"RAW(
---
- name: Spec Example 6.19. Secondary Tag Handle
  from: http://www.yaml.org/spec/1.2/spec.html#id2782940
  tags: spec header tag unknown-tag
  yaml: |
    %TAG !! tag:example.com,2000:app/
    ---
    !!int 1 - 3 # Interval, not integer
  tree: |
    +STR
     +DOC ---
      =VAL <tag:example.com,2000:app/int> :1 - 3
     -DOC
    -STR
  json: |
    "1 - 3"
  dump: |
    --- !<tag:example.com,2000:app/int> 1 - 3
)RAW";

static constexpr std::string_view T_P94K = R"RAW(
---
- name: Spec Example 6.11. Multi-Line Comments
  from: http://www.yaml.org/spec/1.2/spec.html#id2780696
  tags: spec comment
  yaml: |
    key:    # Comment
            # lines
      value
    ↵
    ↵
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL :value
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": "value"
    }
  dump: |
    key: value
)RAW";

static constexpr std::string_view T_PBJ2 = R"RAW(
---
- name: Spec Example 2.3. Mapping Scalars to Sequences
  from: http://www.yaml.org/spec/1.2/spec.html#id2759963
  tags: spec mapping sequence
  yaml: |
    american:
      - Boston Red Sox
      - Detroit Tigers
      - New York Yankees
    national:
      - New York Mets
      - Chicago Cubs
      - Atlanta Braves
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :american
       +SEQ
        =VAL :Boston Red Sox
        =VAL :Detroit Tigers
        =VAL :New York Yankees
       -SEQ
       =VAL :national
       +SEQ
        =VAL :New York Mets
        =VAL :Chicago Cubs
        =VAL :Atlanta Braves
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "american": [
        "Boston Red Sox",
        "Detroit Tigers",
        "New York Yankees"
      ],
      "national": [
        "New York Mets",
        "Chicago Cubs",
        "Atlanta Braves"
      ]
    }
  dump: |
    american:
    - Boston Red Sox
    - Detroit Tigers
    - New York Yankees
    national:
    - New York Mets
    - Chicago Cubs
    - Atlanta Braves
)RAW";

static constexpr std::string_view T_PRH3 = R"RAW(
---
- name: Spec Example 7.9. Single Quoted Lines
  from: http://www.yaml.org/spec/1.2/spec.html#id2788756
  tags: single spec scalar whitespace upto-1.2
  yaml: |
    ' 1st non-empty

     2nd non-empty␣
    ———»3rd non-empty '
  tree: |
    +STR
     +DOC
      =VAL ' 1st non-empty\n2nd non-empty 3rd non-empty␣
     -DOC
    -STR
  json: |
    " 1st non-empty\n2nd non-empty 3rd non-empty "
  dump: |
    ' 1st non-empty

      2nd non-empty 3rd non-empty '
  emit: |
    ' 1st non-empty

      2nd non-empty 3rd non-empty '
)RAW";

static constexpr std::string_view T_PUW8 = R"RAW(
---
- name: Document start on last line
  from: '@perlpunk'
  tags: header
  yaml: |
    ---
    a: b
    ---
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
       =VAL :b
      -MAP
     -DOC
     +DOC ---
      =VAL :
     -DOC
    -STR
  json: |
    {
      "a": "b"
    }
    null
  dump: |
    ---
    a: b
    ---
    ...
)RAW";

static constexpr std::string_view T_PW8X = R"RAW(
---
- name: Anchors on Empty Scalars
  from: NimYAML tests
  tags: anchor explicit-key
  yaml: |
    - &a
    - a
    -
      &a : a
      b: &b
    -
      &c : &a
    -
      ? &d
    -
      ? &e
      : &a
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL &a :
       =VAL :a
       +MAP
        =VAL &a :
        =VAL :a
        =VAL :b
        =VAL &b :
       -MAP
       +MAP
        =VAL &c :
        =VAL &a :
       -MAP
       +MAP
        =VAL &d :
        =VAL :
       -MAP
       +MAP
        =VAL &e :
        =VAL &a :
       -MAP
      -SEQ
     -DOC
    -STR
  dump: |
    - &a
    - a
    - &a : a
      b: &b
    - &c : &a
    - &d :
    - &e : &a
)RAW";

static constexpr std::string_view T_Q4CL = R"RAW(
---
- name: Trailing content after quoted value
  from: '@perlpunk'
  tags: error mapping double
  fail: true
  yaml: |
    key1: "quoted1"
    key2: "quoted2" trailing content
    key3: "quoted3"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key1
       =VAL "quoted1
       =VAL :key2
       =VAL "quoted2
)RAW";

static constexpr std::string_view T_Q5MG = R"RAW(
---
- name: Tab at beginning of line followed by a flow mapping
  from: IRC
  tags: flow whitespace
  yaml: |
    ———»{}
  tree: |
    +STR
     +DOC
      +MAP {}
      -MAP
     -DOC
    -STR
  json: |
    {}
  dump: |
    {}
)RAW";

static constexpr std::string_view T_Q88A = R"RAW(
---
- name: Spec Example 7.23. Flow Content
  from: http://www.yaml.org/spec/1.2/spec.html#id2793163
  tags: spec flow sequence mapping
  yaml: |
    - [ a, b ]
    - { a: b }
    - "a"
    - 'b'
    - c
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        =VAL :a
        =VAL :b
       -SEQ
       +MAP {}
        =VAL :a
        =VAL :b
       -MAP
       =VAL "a
       =VAL 'b
       =VAL :c
      -SEQ
     -DOC
    -STR
  json: |
    [
      [
        "a",
        "b"
      ],
      {
        "a": "b"
      },
      "a",
      "b",
      "c"
    ]
  dump: |
    - - a
      - b
    - a: b
    - "a"
    - 'b'
    - c
)RAW";

static constexpr std::string_view T_Q8AD = R"RAW(
---
- name: Spec Example 7.5. Double Quoted Line Breaks [1.3]
  from: NP9H, modified for YAML 1.3
  tags: double spec scalar whitespace 1.3-mod
  yaml: |
    ---
    "folded␣
    to a space,
    ␣
    to a line feed, or »\
     \ »non-content"
  tree: |
    +STR
     +DOC ---
      =VAL "folded to a space,\nto a line feed, or \t \tnon-content
     -DOC
    -STR
  json: |
    "folded to a space,\nto a line feed, or \t \tnon-content"
  dump: |
    "folded to a space,\nto a line feed, or \t \tnon-content"
  emit: |
    --- "folded to a space,\nto a line feed, or \t \tnon-content"
)RAW";

static constexpr std::string_view T_Q9WF = R"RAW(
---
- name: Spec Example 6.12. Separation Spaces
  from: http://www.yaml.org/spec/1.2/spec.html#id2780989
  tags: complex-key flow spec comment whitespace 1.3-err
  yaml: |
    { first: Sammy, last: Sosa }:
    # Statistics:
      hr:  # Home runs
         65
      avg: # Average
       0.278
  tree: |
    +STR
     +DOC
      +MAP
       +MAP {}
        =VAL :first
        =VAL :Sammy
        =VAL :last
        =VAL :Sosa
       -MAP
       +MAP
        =VAL :hr
        =VAL :65
        =VAL :avg
        =VAL :0.278
       -MAP
      -MAP
     -DOC
    -STR
  dump: |
    ? first: Sammy
      last: Sosa
    : hr: 65
      avg: 0.278
)RAW";

static constexpr std::string_view T_QB6E = R"RAW(
---
- name: Wrong indented multiline quoted scalar
  from: '@perlpunk'
  tags: double error indent
  fail: true
  yaml: |
    ---
    quoted: "a
    b
    c"
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :quoted
)RAW";

static constexpr std::string_view T_QF4Y = R"RAW(
---
- name: Spec Example 7.19. Single Pair Flow Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2792291
  tags: spec flow mapping
  yaml: |
    [
    foo: bar
    ]
  tree: |
    +STR
     +DOC
      +SEQ []
       +MAP {}
        =VAL :foo
        =VAL :bar
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "foo": "bar"
      }
    ]
  dump: |
    - foo: bar
)RAW";

static constexpr std::string_view T_QLJ7 = R"RAW(
---
- name: Tag shorthand used in documents but only defined in the first
  from: IRC
  tags: error directive tag
  fail: true
  yaml: |
    %TAG !prefix! tag:example.com,2011:
    --- !prefix!A
    a: b
    --- !prefix!B
    c: d
    --- !prefix!C
    e: f
  tree: |
    +STR
     +DOC ---
      +MAP <tag:example.com,2011:A>
       =VAL :a
       =VAL :b
      -MAP
     -DOC
     +DOC ---
)RAW";

static constexpr std::string_view T_QT73 = R"RAW(
---
- name: Comment and document-end marker
  from: '@perlpunk'
  tags: comment footer
  yaml: |
    # comment
    ...
  tree: |
    +STR
    -STR
  json: ''
  dump: ''
)RAW";

static constexpr std::string_view T_R4YG = R"RAW(
---
- name: Spec Example 8.2. Block Indentation Indicator
  from: http://www.yaml.org/spec/1.2/spec.html#id2794311
  tags: spec literal folded scalar whitespace libyaml-err upto-1.2
  yaml: |
    - |
     detected
    - >
    ␣
    ␣␣
      # detected
    - |1
      explicit
    - >
     ——»
     detected
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL |detected\n
       =VAL >\n\n# detected\n
       =VAL | explicit\n
       =VAL >\t\ndetected\n
      -SEQ
     -DOC
    -STR
  json: |
    [
      "detected\n",
      "\n\n# detected\n",
      " explicit\n",
      "\t\ndetected\n"
    ]
  dump: |
    - |
      detected
    - >2


      # detected
    - |2
       explicit
    - "\t\ndetected\n"
)RAW";

static constexpr std::string_view T_R52L = R"RAW(
---
- name: Nested flow mapping sequence and mappings
  from: '@perlpunk'
  tags: flow mapping sequence
  yaml: |
    ---
    { top1: [item1, {key2: value2}, item3], top2: value2 }
  tree: |
    +STR
     +DOC ---
      +MAP {}
       =VAL :top1
       +SEQ []
        =VAL :item1
        +MAP {}
         =VAL :key2
         =VAL :value2
        -MAP
        =VAL :item3
       -SEQ
       =VAL :top2
       =VAL :value2
      -MAP
     -DOC
    -STR
  json: |
    {
      "top1": [
        "item1",
        {
          "key2": "value2"
        },
        "item3"
      ],
      "top2": "value2"
    }
  dump: |
    ---
    top1:
    - item1
    - key2: value2
    - item3
    top2: value2
)RAW";

static constexpr std::string_view T_RHX7 = R"RAW(
---
- name: YAML directive without document end marker
  from: '@perlpunk'
  tags: directive error
  fail: true
  yaml: |
    ---
    key: value
    %YAML 1.2
    ---
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key
       =VAL :value
)RAW";

static constexpr std::string_view T_RLU9 = R"RAW(
---
- name: Sequence Indent
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/indent.tml
  tags: sequence indent
  yaml: |
    foo:
    - 42
    bar:
      - 44
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       +SEQ
        =VAL :42
       -SEQ
       =VAL :bar
       +SEQ
        =VAL :44
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": [
        42
      ],
      "bar": [
        44
      ]
    }
  dump: |
    foo:
    - 42
    bar:
    - 44
)RAW";

static constexpr std::string_view T_RR7F = R"RAW(
---
- name: Mixed Block Mapping (implicit to explicit)
  from: NimYAML tests
  tags: explicit-key mapping
  yaml: |
    a: 4.2
    ? d
    : 23
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL :4.2
       =VAL :d
       =VAL :23
      -MAP
     -DOC
    -STR
  json: |
    {
      "d": 23,
      "a": 4.2
    }
  dump: |
    a: 4.2
    d: 23
)RAW";

static constexpr std::string_view T_RTP8 = R"RAW(
---
- name: Spec Example 9.2. Document Markers
  from: http://www.yaml.org/spec/1.2/spec.html#id2800866
  tags: spec header footer
  yaml: |
    %YAML 1.2
    ---
    Document
    ... # Suffix
  tree: |
    +STR
     +DOC ---
      =VAL :Document
     -DOC ...
    -STR
  json: |
    "Document"
  dump: |
    --- Document
    ...
)RAW";

static constexpr std::string_view T_RXY3 = R"RAW(
---
- name: Invalid document-end marker in single quoted string
  from: '@perlpunk'
  tags: footer single error
  fail: true
  yaml: |
    ---
    '
    ...
    '
  tree: |
    +STR
     +DOC ---
)RAW";

static constexpr std::string_view T_RZP5 = R"RAW(
---
- name: Various Trailing Comments [1.3]
  from: XW4D, modified for YAML 1.3
  tags: anchor comment folded mapping 1.3-mod
  yaml: |
    a: "double
      quotes" # lala
    b: plain
     value  # lala
    c  : #lala
      d
    ? # lala
     - seq1
    : # lala
     - #lala
      seq2
    e: &node # lala
     - x: y
    block: > # lala
      abcde
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL "double quotes
       =VAL :b
       =VAL :plain value
       =VAL :c
       =VAL :d
       +SEQ
        =VAL :seq1
       -SEQ
       +SEQ
        =VAL :seq2
       -SEQ
       =VAL :e
       +SEQ &node
        +MAP
         =VAL :x
         =VAL :y
        -MAP
       -SEQ
       =VAL :block
       =VAL >abcde\n
      -MAP
     -DOC
    -STR
  dump: |
    a: "double quotes"
    b: plain value
    c: d
    ? - seq1
    : - seq2
    e: &node
    - x: y
    block: >
      abcde
)RAW";

static constexpr std::string_view T_RZT7 = R"RAW(
---
- name: Spec Example 2.28. Log File
  from: http://www.yaml.org/spec/1.2/spec.html#id2761866
  tags: spec header literal mapping sequence
  yaml: |
    ---
    Time: 2001-11-23 15:01:42 -5
    User: ed
    Warning:
      This is an error message
      for the log file
    ---
    Time: 2001-11-23 15:02:31 -5
    User: ed
    Warning:
      A slightly different error
      message.
    ---
    Date: 2001-11-23 15:03:17 -5
    User: ed
    Fatal:
      Unknown variable "bar"
    Stack:
      - file: TopClass.py
        line: 23
        code: |
          x = MoreObject("345\n")
      - file: MoreClass.py
        line: 58
        code: |-
          foo = bar
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :Time
       =VAL :2001-11-23 15:01:42 -5
       =VAL :User
       =VAL :ed
       =VAL :Warning
       =VAL :This is an error message for the log file
      -MAP
     -DOC
     +DOC ---
      +MAP
       =VAL :Time
       =VAL :2001-11-23 15:02:31 -5
       =VAL :User
       =VAL :ed
       =VAL :Warning
       =VAL :A slightly different error message.
      -MAP
     -DOC
     +DOC ---
      +MAP
       =VAL :Date
       =VAL :2001-11-23 15:03:17 -5
       =VAL :User
       =VAL :ed
       =VAL :Fatal
       =VAL :Unknown variable "bar"
       =VAL :Stack
       +SEQ
        +MAP
         =VAL :file
         =VAL :TopClass.py
         =VAL :line
         =VAL :23
         =VAL :code
         =VAL |x = MoreObject("345\\n")\n
        -MAP
        +MAP
         =VAL :file
         =VAL :MoreClass.py
         =VAL :line
         =VAL :58
         =VAL :code
         =VAL |foo = bar
        -MAP
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "Time": "2001-11-23 15:01:42 -5",
      "User": "ed",
      "Warning": "This is an error message for the log file"
    }
    {
      "Time": "2001-11-23 15:02:31 -5",
      "User": "ed",
      "Warning": "A slightly different error message."
    }
    {
      "Date": "2001-11-23 15:03:17 -5",
      "User": "ed",
      "Fatal": "Unknown variable \"bar\"",
      "Stack": [
        {
          "file": "TopClass.py",
          "line": 23,
          "code": "x = MoreObject(\"345\\n\")\n"
        },
        {
          "file": "MoreClass.py",
          "line": 58,
          "code": "foo = bar"
        }
      ]
    }
  dump: |
    ---
    Time: 2001-11-23 15:01:42 -5
    User: ed
    Warning: This is an error message for the log file
    ---
    Time: 2001-11-23 15:02:31 -5
    User: ed
    Warning: A slightly different error message.
    ---
    Date: 2001-11-23 15:03:17 -5
    User: ed
    Fatal: Unknown variable "bar"
    Stack:
    - file: TopClass.py
      line: 23
      code: |
        x = MoreObject("345\n")
    - file: MoreClass.py
      line: 58
      code: |-
        foo = bar
)RAW";

static constexpr std::string_view T_S3PD = R"RAW(
---
- name: Spec Example 8.18. Implicit Block Mapping Entries
  from: http://www.yaml.org/spec/1.2/spec.html#id2798896
  tags: empty-key spec mapping
  yaml: |
    plain key: in-line value
    : # Both empty
    "quoted key":
    - entry
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :plain key
       =VAL :in-line value
       =VAL :
       =VAL :
       =VAL "quoted key
       +SEQ
        =VAL :entry
       -SEQ
      -MAP
     -DOC
    -STR
  emit: |
    plain key: in-line value
    :
    "quoted key":
    - entry
)RAW";

static constexpr std::string_view T_S4GJ = R"RAW(
---
- name: Invalid text after block scalar indicator
  from: '@perlpunk'
  tags: error folded
  fail: true
  yaml: |
    ---
    folded: > first line
      second line
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :folded
)RAW";

static constexpr std::string_view T_S4JQ = R"RAW(
---
- name: Spec Example 6.28. Non-Specific Tags
  from: http://www.yaml.org/spec/1.2/spec.html#id2785512
  tags: spec tag
  yaml: |
    # Assuming conventional resolution:
    - "12"
    - 12
    - ! 12
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL "12
       =VAL :12
       =VAL <!> :12
      -SEQ
     -DOC
    -STR
  json: |
    [
      "12",
      12,
      "12"
    ]
  dump: |
    - "12"
    - 12
    - ! 12
)RAW";

static constexpr std::string_view T_S4T7 = R"RAW(
---
- name: Document with footer
  from: https://github.com/ingydotnet/yaml-pegex-pm/blob/master/test/footer.tml
  tags: mapping footer
  yaml: |
    aaa: bbb
    ...
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :aaa
       =VAL :bbb
      -MAP
     -DOC ...
    -STR
  json: |
    {
      "aaa": "bbb"
    }
)RAW";

static constexpr std::string_view T_S7BG = R"RAW(
---
- name: Colon followed by comma
  from: '@perlpunk'
  tags: scalar
  yaml: |
    ---
    - :,
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL ::,
      -SEQ
     -DOC
    -STR
  json: |
    [
      ":,"
    ]
  dump: |
    ---
    - :,
)RAW";

static constexpr std::string_view T_S98Z = R"RAW(
---
- name: Block scalar with more spaces than first content line
  from: '@perlpunk'
  tags: error folded comment scalar whitespace
  fail: true
  yaml: |
    empty block scalar: >
    ␣
    ␣␣
    ␣␣␣
     # comment
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :empty block scalar
)RAW";

static constexpr std::string_view T_S9E8 = R"RAW(
---
- name: Spec Example 5.3. Block Structure Indicators
  from: http://www.yaml.org/spec/1.2/spec.html#id2772312
  tags: explicit-key spec mapping sequence
  yaml: |
    sequence:
    - one
    - two
    mapping:
      ? sky
      : blue
      sea : green
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :sequence
       +SEQ
        =VAL :one
        =VAL :two
       -SEQ
       =VAL :mapping
       +MAP
        =VAL :sky
        =VAL :blue
        =VAL :sea
        =VAL :green
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "sequence": [
        "one",
        "two"
      ],
      "mapping": {
        "sky": "blue",
        "sea": "green"
      }
    }
  dump: |
    sequence:
    - one
    - two
    mapping:
      sky: blue
      sea: green
)RAW";

static constexpr std::string_view T_SBG9 = R"RAW(
---
- name: Flow Sequence in Flow Mapping
  from: NimYAML tests
  tags: complex-key sequence mapping flow
  yaml: |
    {a: [b, c], [d, e]: f}
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :a
       +SEQ []
        =VAL :b
        =VAL :c
       -SEQ
       +SEQ []
        =VAL :d
        =VAL :e
       -SEQ
       =VAL :f
      -MAP
     -DOC
    -STR
  dump: |
    a:
    - b
    - c
    ? - d
      - e
    : f
)RAW";

static constexpr std::string_view T_SF5V = R"RAW(
---
- name: Duplicate YAML directive
  from: '@perlpunk'
  tags: directive error
  fail: true
  yaml: |
    %YAML 1.2
    %YAML 1.2
    ---
  tree: |
    +STR
)RAW";

static constexpr std::string_view T_SKE5 = R"RAW(
---
- name: Anchor before zero indented sequence
  from: '@perlpunk'
  tags: anchor indent sequence
  yaml: |
    ---
    seq:
     &anchor
    - a
    - b
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :seq
       +SEQ &anchor
        =VAL :a
        =VAL :b
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "seq": [
        "a",
        "b"
      ]
    }
  dump: |
    ---
    seq: &anchor
    - a
    - b
)RAW";

static constexpr std::string_view T_SM9W = R"RAW(
---
- name: Single character streams
  from: '@ingydotnet'
  tags: sequence
  yaml: |
    -∎
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :
      -SEQ
     -DOC
    -STR
  json: |
    [null]
  dump: |
    -

- tags: mapping
  yaml: |
    :∎
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :
       =VAL :
      -MAP
     -DOC
    -STR
  json: null
  dump: |
    :
)RAW";

static constexpr std::string_view T_SR86 = R"RAW(
---
- name: Anchor plus Alias
  from: '@perlpunk'
  tags: alias error
  fail: true
  yaml: |
    key1: &a value
    key2: &b *a
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key1
       =VAL &a :value
       =VAL :key2
)RAW";

static constexpr std::string_view T_SSW6 = R"RAW(
---
- name: Spec Example 7.7. Single Quoted Characters [1.3]
  from: 4GC6, modified for YAML 1.3
  tags: spec scalar single 1.3-mod
  yaml: |
    ---
    'here''s to "quotes"'
  tree: |
    +STR
     +DOC ---
      =VAL 'here's to "quotes"
     -DOC
    -STR
  json: |
    "here's to \"quotes\""
  dump: |
    --- 'here''s to "quotes"'
)RAW";

static constexpr std::string_view T_SU5Z = R"RAW(
---
- name: Comment without whitespace after doublequoted scalar
  from: '@perlpunk'
  tags: comment error double whitespace
  fail: true
  yaml: |
    key: "value"# invalid comment
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key
       =VAL "value
)RAW";

static constexpr std::string_view T_SU74 = R"RAW(
---
- name: Anchor and alias as mapping key
  from: '@perlpunk'
  tags: error anchor alias mapping
  fail: true
  yaml: |
    key1: &alias value1
    &b *alias : value2
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :key1
       =VAL &alias :value1
)RAW";

static constexpr std::string_view T_SY6V = R"RAW(
---
- name: Anchor before sequence entry on same line
  from: '@perlpunk'
  tags: anchor error sequence
  fail: true
  yaml: |
    &anchor - sequence entry
  tree: |
    +STR
)RAW";

static constexpr std::string_view T_SYW4 = R"RAW(
---
- name: Spec Example 2.2. Mapping Scalars to Scalars
  from: http://www.yaml.org/spec/1.2/spec.html#id2759963
  tags: spec scalar comment
  yaml: |
    hr:  65    # Home runs
    avg: 0.278 # Batting average
    rbi: 147   # Runs Batted In
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :hr
       =VAL :65
       =VAL :avg
       =VAL :0.278
       =VAL :rbi
       =VAL :147
      -MAP
     -DOC
    -STR
  json: |
    {
      "hr": 65,
      "avg": 0.278,
      "rbi": 147
    }
  dump: |
    hr: 65
    avg: 0.278
    rbi: 147
)RAW";

static constexpr std::string_view T_T26H = R"RAW(
---
- name: Spec Example 8.8. Literal Content [1.3]
  from: DWX9, modified for YAML 1.3
  tags: spec literal scalar comment whitespace 1.3-mod
  yaml: |
    --- |
    ␣
    ␣␣
      literal
    ␣␣␣
    ␣␣
      text

     # Comment
  tree: |
    +STR
     +DOC ---
      =VAL |\n\nliteral\n \n\ntext\n
     -DOC
    -STR
  json: |
    "\n\nliteral\n \n\ntext\n"
  dump: |
    "\n\nliteral\n \n\ntext\n"
  emit: |
    --- |


      literal
    ␣␣␣

      text
)RAW";

static constexpr std::string_view T_T4YY = R"RAW(
---
- name: Spec Example 7.9. Single Quoted Lines [1.3]
  from: PRH3, modified for YAML 1.3
  tags: single spec scalar whitespace 1.3-mod
  yaml: |
    ---
    ' 1st non-empty

     2nd non-empty␣
     3rd non-empty '
  tree: |
    +STR
     +DOC ---
      =VAL ' 1st non-empty\n2nd non-empty 3rd non-empty␣
     -DOC
    -STR
  json: |
    " 1st non-empty\n2nd non-empty 3rd non-empty "
  dump: |
    ' 1st non-empty

      2nd non-empty 3rd non-empty '
  emit: |
    --- ' 1st non-empty

      2nd non-empty 3rd non-empty '
)RAW";

static constexpr std::string_view T_T5N4 = R"RAW(
---
- name: Spec Example 8.7. Literal Scalar [1.3]
  from: M9B4, modified for YAML 1.3
  tags: spec literal scalar whitespace 1.3-mod
  yaml: |
    --- |
     literal
     ——»text
    ↵
    ↵
  tree: |
    +STR
     +DOC ---
      =VAL |literal\n\ttext\n
     -DOC
    -STR
  json: |
    "literal\n\ttext\n"
  dump: |
    "literal\n\ttext\n"
  emit: |
    --- |
      literal
      —»text
)RAW";

static constexpr std::string_view T_T833 = R"RAW(
---
- name: Flow mapping missing a separating comma
  from: '@perlpunk'
  tags: error flow mapping
  fail: true
  yaml: |
    ---
    {
     foo: 1
     bar: 2 }
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :foo
)RAW";

static constexpr std::string_view T_TD5N = R"RAW(
---
- name: Invalid scalar after sequence
  from: '@perlpunk'
  tags: error sequence scalar
  fail: true
  yaml: |
    - item1
    - item2
    invalid
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :item1
       =VAL :item2
)RAW";

static constexpr std::string_view T_TE2A = R"RAW(
---
- name: Spec Example 8.16. Block Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2798147
  tags: spec mapping
  yaml: |
    block mapping:
     key: value
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :block mapping
       +MAP
        =VAL :key
        =VAL :value
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "block mapping": {
        "key": "value"
      }
    }
  dump: |
    block mapping:
      key: value
)RAW";

static constexpr std::string_view T_TL85 = R"RAW(
---
- name: Spec Example 6.8. Flow Folding
  from: http://www.yaml.org/spec/1.2/spec.html#id2779950
  tags: double spec whitespace scalar upto-1.2
  yaml: |
    "
      foo␣
    ␣
      —» bar

      baz
    "
  tree: |
    +STR
     +DOC
      =VAL " foo\nbar\nbaz␣
     -DOC
    -STR
  json: |
    " foo\nbar\nbaz "
  dump: |
    " foo\nbar\nbaz "
)RAW";

static constexpr std::string_view T_TS54 = R"RAW(
---
- name: Folded Block Scalar
  from: NimYAML tests
  tags: folded scalar 1.3-err
  yaml: |
    >
     ab
     cd
    ␣
     ef


     gh
  tree: |
    +STR
     +DOC
      =VAL >ab cd\nef\n\ngh\n
     -DOC
    -STR
  json: |
    "ab cd\nef\n\ngh\n"
  dump: |
    >
      ab cd

      ef


      gh
)RAW";

static constexpr std::string_view T_U3C3 = R"RAW(
---
- name: Spec Example 6.16. “TAG” directive
  from: http://www.yaml.org/spec/1.2/spec.html#id2782252
  tags: spec header tag
  yaml: |
    %TAG !yaml! tag:yaml.org,2002:
    ---
    !yaml!str "foo"
  tree: |
    +STR
     +DOC ---
      =VAL <tag:yaml.org,2002:str> "foo
     -DOC
    -STR
  json: |
    "foo"
  dump: |
    --- !!str "foo"
)RAW";

static constexpr std::string_view T_U3XV = R"RAW(
---
- name: Node and Mapping Key Anchors
  from: '@perlpunk'
  tags: anchor comment 1.3-err
  yaml: |
    ---
    top1: &node1
      &k1 key1: one
    top2: &node2 # comment
      key2: two
    top3:
      &k3 key3: three
    top4:
      &node4
      &k4 key4: four
    top5:
      &node5
      key5: five
    top6: &val6
      six
    top7:
      &val7 seven
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :top1
       +MAP &node1
        =VAL &k1 :key1
        =VAL :one
       -MAP
       =VAL :top2
       +MAP &node2
        =VAL :key2
        =VAL :two
       -MAP
       =VAL :top3
       +MAP
        =VAL &k3 :key3
        =VAL :three
       -MAP
       =VAL :top4
       +MAP &node4
        =VAL &k4 :key4
        =VAL :four
       -MAP
       =VAL :top5
       +MAP &node5
        =VAL :key5
        =VAL :five
       -MAP
       =VAL :top6
       =VAL &val6 :six
       =VAL :top7
       =VAL &val7 :seven
      -MAP
     -DOC
    -STR
  json: |
    {
      "top1": {
        "key1": "one"
      },
      "top2": {
        "key2": "two"
      },
      "top3": {
        "key3": "three"
      },
      "top4": {
        "key4": "four"
      },
      "top5": {
        "key5": "five"
      },
      "top6": "six",
      "top7": "seven"
    }
  dump: |
    ---
    top1: &node1
      &k1 key1: one
    top2: &node2
      key2: two
    top3:
      &k3 key3: three
    top4: &node4
      &k4 key4: four
    top5: &node5
      key5: five
    top6: &val6 six
    top7: &val7 seven
)RAW";

static constexpr std::string_view T_U44R = R"RAW(
---
- name: Bad indentation in mapping (2)
  from: '@perlpunk'
  tags: error mapping indent double
  fail: true
  yaml: |
    map:
      key1: "quoted1"
       key2: "bad indentation"
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :map
       +MAP
        =VAL :key1
        =VAL "quoted1
)RAW";

static constexpr std::string_view T_U99R = R"RAW(
---
- name: Invalid comma in tag
  from: '@perlpunk'
  tags: error tag
  fail: true
  yaml: |
    - !!str, xxx
  tree: |
    +STR
     +DOC
      +SEQ
)RAW";

static constexpr std::string_view T_U9NS = R"RAW(
---
- name: Spec Example 2.8. Play by Play Feed from a Game
  from: http://www.yaml.org/spec/1.2/spec.html#id2760519
  tags: spec header
  yaml: |
    ---
    time: 20:03:20
    player: Sammy Sosa
    action: strike (miss)
    ...
    ---
    time: 20:03:47
    player: Sammy Sosa
    action: grand slam
    ...
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :time
       =VAL :20:03:20
       =VAL :player
       =VAL :Sammy Sosa
       =VAL :action
       =VAL :strike (miss)
      -MAP
     -DOC ...
     +DOC ---
      +MAP
       =VAL :time
       =VAL :20:03:47
       =VAL :player
       =VAL :Sammy Sosa
       =VAL :action
       =VAL :grand slam
      -MAP
     -DOC ...
    -STR
  json: |
    {
      "time": "20:03:20",
      "player": "Sammy Sosa",
      "action": "strike (miss)"
    }
    {
      "time": "20:03:47",
      "player": "Sammy Sosa",
      "action": "grand slam"
    }
)RAW";

static constexpr std::string_view T_UDM2 = R"RAW(
---
- name: Plain URL in flow mapping
  from: https://github.com/yaml/libyaml/pull/28
  tags: flow scalar
  yaml: |
    - { url: http://example.org }
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP {}
        =VAL :url
        =VAL :http://example.org
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      {
        "url": "http://example.org"
      }
    ]
  dump: |
    - url: http://example.org
)RAW";

static constexpr std::string_view T_UDR7 = R"RAW(
---
- name: Spec Example 5.4. Flow Collection Indicators
  from: http://www.yaml.org/spec/1.2/spec.html#id2772813
  tags: spec flow sequence mapping
  yaml: |
    sequence: [ one, two, ]
    mapping: { sky: blue, sea: green }
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :sequence
       +SEQ []
        =VAL :one
        =VAL :two
       -SEQ
       =VAL :mapping
       +MAP {}
        =VAL :sky
        =VAL :blue
        =VAL :sea
        =VAL :green
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "sequence": [
        "one",
        "two"
      ],
      "mapping": {
        "sky": "blue",
        "sea": "green"
      }
    }
  dump: |
    sequence:
    - one
    - two
    mapping:
      sky: blue
      sea: green
)RAW";

static constexpr std::string_view T_UGM3 = R"RAW(
---
- name: Spec Example 2.27. Invoice
  from: http://www.yaml.org/spec/1.2/spec.html#id2761823
  tags: spec tag literal mapping sequence alias unknown-tag
  yaml: |
    --- !<tag:clarkevans.com,2002:invoice>
    invoice: 34843
    date   : 2001-01-23
    bill-to: &id001
        given  : Chris
        family : Dumars
        address:
            lines: |
                458 Walkman Dr.
                Suite #292
            city    : Royal Oak
            state   : MI
            postal  : 48046
    ship-to: *id001
    product:
        - sku         : BL394D
          quantity    : 4
          description : Basketball
          price       : 450.00
        - sku         : BL4438H
          quantity    : 1
          description : Super Hoop
          price       : 2392.00
    tax  : 251.42
    total: 4443.52
    comments:
        Late afternoon is best.
        Backup contact is Nancy
        Billsmer @ 338-4338.
  tree: |
    +STR
     +DOC ---
      +MAP <tag:clarkevans.com,2002:invoice>
       =VAL :invoice
       =VAL :34843
       =VAL :date
       =VAL :2001-01-23
       =VAL :bill-to
       +MAP &id001
        =VAL :given
        =VAL :Chris
        =VAL :family
        =VAL :Dumars
        =VAL :address
        +MAP
         =VAL :lines
         =VAL |458 Walkman Dr.\nSuite #292\n
         =VAL :city
         =VAL :Royal Oak
         =VAL :state
         =VAL :MI
         =VAL :postal
         =VAL :48046
        -MAP
       -MAP
       =VAL :ship-to
       =ALI *id001
       =VAL :product
       +SEQ
        +MAP
         =VAL :sku
         =VAL :BL394D
         =VAL :quantity
         =VAL :4
         =VAL :description
         =VAL :Basketball
         =VAL :price
         =VAL :450.00
        -MAP
        +MAP
         =VAL :sku
         =VAL :BL4438H
         =VAL :quantity
         =VAL :1
         =VAL :description
         =VAL :Super Hoop
         =VAL :price
         =VAL :2392.00
        -MAP
       -SEQ
       =VAL :tax
       =VAL :251.42
       =VAL :total
       =VAL :4443.52
       =VAL :comments
       =VAL :Late afternoon is best. Backup contact is Nancy Billsmer @ 338-4338.
      -MAP
     -DOC
    -STR
  json: |
    {
      "invoice": 34843,
      "date": "2001-01-23",
      "bill-to": {
        "given": "Chris",
        "family": "Dumars",
        "address": {
          "lines": "458 Walkman Dr.\nSuite #292\n",
          "city": "Royal Oak",
          "state": "MI",
          "postal": 48046
        }
      },
      "ship-to": {
        "given": "Chris",
        "family": "Dumars",
        "address": {
          "lines": "458 Walkman Dr.\nSuite #292\n",
          "city": "Royal Oak",
          "state": "MI",
          "postal": 48046
        }
      },
      "product": [
        {
          "sku": "BL394D",
          "quantity": 4,
          "description": "Basketball",
          "price": 450
        },
        {
          "sku": "BL4438H",
          "quantity": 1,
          "description": "Super Hoop",
          "price": 2392
        }
      ],
      "tax": 251.42,
      "total": 4443.52,
      "comments": "Late afternoon is best. Backup contact is Nancy Billsmer @ 338-4338."
    }
  dump: |
    --- !<tag:clarkevans.com,2002:invoice>
    invoice: 34843
    date: 2001-01-23
    bill-to: &id001
      given: Chris
      family: Dumars
      address:
        lines: |
          458 Walkman Dr.
          Suite #292
        city: Royal Oak
        state: MI
        postal: 48046
    ship-to: *id001
    product:
    - sku: BL394D
      quantity: 4
      description: Basketball
      price: 450.00
    - sku: BL4438H
      quantity: 1
      description: Super Hoop
      price: 2392.00
    tax: 251.42
    total: 4443.52
    comments: Late afternoon is best. Backup contact is Nancy Billsmer @ 338-4338.
)RAW";

static constexpr std::string_view T_UKK6 = R"RAW(
---
- name: Syntax character edge cases
  from: '@ingydotnet'
  tags: edge empty-key
  yaml: |
    - :
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :
        =VAL :
       -MAP
      -SEQ
     -DOC
    -STR

- yaml: |
    ::
  tree: |
    +STR
     +DOC
      +MAP
       =VAL ::
       =VAL :
      -MAP
     -DOC
    -STR
  json: |
    {
      ":": null
    }

- yaml: |
    !
  tree: |
    +STR
     +DOC
      =VAL <!> :
     -DOC
    -STR
  json: null
)RAW";

static constexpr std::string_view T_UT92 = R"RAW(
---
- name: Spec Example 9.4. Explicit Documents
  from: http://www.yaml.org/spec/1.2/spec.html#id2801448
  tags: flow spec header footer comment
  yaml: |
    ---
    { matches
    % : 20 }
    ...
    ---
    # Empty
    ...
  tree: |
    +STR
     +DOC ---
      +MAP {}
       =VAL :matches %
       =VAL :20
      -MAP
     -DOC ...
     +DOC ---
      =VAL :
     -DOC ...
    -STR
  json: |
    {
      "matches %": 20
    }
    null
  dump: |
    ---
    matches %: 20
    ...
    ---
    ...
)RAW";

static constexpr std::string_view T_UV7Q = R"RAW(
---
- name: Legal tab after indentation
  from: '@ingydotnet'
  tags: indent whitespace
  yaml: |
    x:
     - x
      ——»x
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :x
       +SEQ
        =VAL :x x
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "x": [
        "x x"
      ]
    }
  dump: |
    x:
    - x x
)RAW";

static constexpr std::string_view T_V55R = R"RAW(
---
- name: Aliases in Block Sequence
  from: NimYAML tests
  tags: alias sequence
  yaml: |
    - &a a
    - &b b
    - *a
    - *b
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL &a :a
       =VAL &b :b
       =ALI *a
       =ALI *b
      -SEQ
     -DOC
    -STR
  json: |
    [
      "a",
      "b",
      "a",
      "b"
    ]
)RAW";

static constexpr std::string_view T_V9D5 = R"RAW(
---
- name: Spec Example 8.19. Compact Block Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2799091
  tags: complex-key explicit-key spec mapping
  yaml: |
    - sun: yellow
    - ? earth: blue
      : moon: white
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :sun
        =VAL :yellow
       -MAP
       +MAP
        +MAP
         =VAL :earth
         =VAL :blue
        -MAP
        +MAP
         =VAL :moon
         =VAL :white
        -MAP
       -MAP
      -SEQ
     -DOC
    -STR
)RAW";

static constexpr std::string_view T_VJP3 = R"RAW(
---
- name: Flow collections over many lines
  from: '@ingydotnet'
  tags: flow indent
  fail: true
  yaml: |
    k: {
    k
    :
    v
    }
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :k
       +MAP {}

- yaml: |
    k: {
     k
     :
     v
     }
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :k
       +MAP {}
        =VAL :k
        =VAL :v
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "k" : {
        "k" : "v"
      }
    }
  dump: |
    ---
    k:
      k: v
  emit: |
    k:
      k: v
)RAW";

static constexpr std::string_view T_W42U = R"RAW(
---
- name: Spec Example 8.15. Block Sequence Entry Types
  from: http://www.yaml.org/spec/1.2/spec.html#id2797944
  tags: comment spec literal sequence
  yaml: |
    - # Empty
    - |
     block node
    - - one # Compact
      - two # sequence
    - one: two # Compact mapping
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :
       =VAL |block node\n
       +SEQ
        =VAL :one
        =VAL :two
       -SEQ
       +MAP
        =VAL :one
        =VAL :two
       -MAP
      -SEQ
     -DOC
    -STR
  json: |
    [
      null,
      "block node\n",
      [
        "one",
        "two"
      ],
      {
        "one": "two"
      }
    ]
  dump: |
    -
    - |
      block node
    - - one
      - two
    - one: two
)RAW";

static constexpr std::string_view T_W4TN = R"RAW(
---
- name: Spec Example 9.5. Directives Documents
  from: http://www.yaml.org/spec/1.2/spec.html#id2801606
  tags: spec header footer 1.3-err
  yaml: |
    %YAML 1.2
    --- |
    %!PS-Adobe-2.0
    ...
    %YAML 1.2
    ---
    # Empty
    ...
  tree: |
    +STR
     +DOC ---
      =VAL |%!PS-Adobe-2.0\n
     -DOC ...
     +DOC ---
      =VAL :
     -DOC ...
    -STR
  json: |
    "%!PS-Adobe-2.0\n"
    null
  dump: |
    --- |
      %!PS-Adobe-2.0
    ...
    ---
    ...
)RAW";

static constexpr std::string_view T_W5VH = R"RAW(
---
- name: Allowed characters in alias
  from: '@perlpunk'
  tags: alias 1.3-err
  yaml: |
    a: &:@*!$"<foo>: scalar a
    b: *:@*!$"<foo>:
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL &:@*!$"<foo>: :scalar a
       =VAL :b
       =ALI *:@*!$"<foo>:
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": "scalar a",
      "b": "scalar a"
    }
)RAW";

static constexpr std::string_view T_W9L4 = R"RAW(
---
- name: Literal block scalar with more spaces in first line
  from: '@perlpunk'
  tags: error literal whitespace
  fail: true
  yaml: |
    ---
    block scalar: |
    ␣␣␣␣␣
      more spaces at the beginning
      are invalid
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :block scalar
)RAW";

static constexpr std::string_view T_WZ62 = R"RAW(
---
- name: Spec Example 7.2. Empty Content
  from: http://www.yaml.org/spec/1.2/spec.html#id2786720
  tags: spec flow scalar tag
  yaml: |
    {
      foo : !!str,
      !!str : bar,
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :foo
       =VAL <tag:yaml.org,2002:str> :
       =VAL <tag:yaml.org,2002:str> :
       =VAL :bar
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "",
      "": "bar"
    }
  dump: |
    foo: !!str
    !!str : bar
)RAW";

static constexpr std::string_view T_X38W = R"RAW(
---
- name: Aliases in Flow Objects
  from: NimYAML tests
  tags: alias complex-key flow
  yaml: |
    { &a [a, &b b]: *b, *a : [c, *b, d]}
  tree: |
    +STR
     +DOC
      +MAP {}
       +SEQ [] &a
        =VAL :a
        =VAL &b :b
       -SEQ
       =ALI *b
       =ALI *a
       +SEQ []
        =VAL :c
        =ALI *b
        =VAL :d
       -SEQ
      -MAP
     -DOC
    -STR
  dump: |
    ? &a
    - a
    - &b b
    : *b
    *a :
    - c
    - *b
    - d
)RAW";

static constexpr std::string_view T_X4QW = R"RAW(
---
- name: Comment without whitespace after block scalar indicator
  from: '@perlpunk'
  tags: folded comment error whitespace
  fail: true
  yaml: |
    block: ># comment
      scalar
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :block
)RAW";

static constexpr std::string_view T_X8DW = R"RAW(
---
- name: Explicit key and value seperated by comment
  from: '@perlpunk'
  tags: comment explicit-key mapping
  yaml: |
    ---
    ? key
    # comment
    : value
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key
       =VAL :value
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": "value"
    }
  dump: |
    ---
    key: value
)RAW";

static constexpr std::string_view T_XLQ9 = R"RAW(
---
- name: Multiline scalar that looks like a YAML directive
  from: '@perlpunk'
  tags: directive scalar
  yaml: |
    ---
    scalar
    %YAML 1.2
  tree: |
    +STR
     +DOC ---
      =VAL :scalar %YAML 1.2
     -DOC
    -STR
  json: |
    "scalar %YAML 1.2"
  dump: |
    --- scalar %YAML 1.2
    ...
)RAW";

static constexpr std::string_view T_XV9V = R"RAW(
---
- name: Spec Example 6.5. Empty Lines [1.3]
  from: 5GBF, modified for YAML 1.3
  tags: literal spec scalar 1.3-mod
  yaml: |
    Folding:
      "Empty line

      as a line feed"
    Chomping: |
      Clipped empty lines
    ␣
    ↵
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :Folding
       =VAL "Empty line\nas a line feed
       =VAL :Chomping
       =VAL |Clipped empty lines\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "Folding": "Empty line\nas a line feed",
      "Chomping": "Clipped empty lines\n"
    }
  dump: |
    Folding: "Empty line\nas a line feed"
    Chomping: |
      Clipped empty lines
)RAW";

static constexpr std::string_view T_XW4D = R"RAW(
---
- name: Various Trailing Comments
  from: '@perlpunk'
  tags: comment explicit-key folded 1.3-err
  yaml: |
    a: "double
      quotes" # lala
    b: plain
     value  # lala
    c  : #lala
      d
    ? # lala
     - seq1
    : # lala
     - #lala
      seq2
    e:
     &node # lala
     - x: y
    block: > # lala
      abcde
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
       =VAL "double quotes
       =VAL :b
       =VAL :plain value
       =VAL :c
       =VAL :d
       +SEQ
        =VAL :seq1
       -SEQ
       +SEQ
        =VAL :seq2
       -SEQ
       =VAL :e
       +SEQ &node
        +MAP
         =VAL :x
         =VAL :y
        -MAP
       -SEQ
       =VAL :block
       =VAL >abcde\n
      -MAP
     -DOC
    -STR
  dump: |
    a: "double quotes"
    b: plain value
    c: d
    ? - seq1
    : - seq2
    e: &node
    - x: y
    block: >
      abcde
)RAW";

static constexpr std::string_view T_Y2GN = R"RAW(
---
- name: Anchor with colon in the middle
  from: '@perlpunk'
  tags: anchor
  yaml: |
    ---
    key: &an:chor value
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :key
       =VAL &an:chor :value
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": "value"
    }
  dump: |
    ---
    key: &an:chor value
)RAW";

static constexpr std::string_view T_Y79Y = R"RAW(
---
- name: Tabs in various contexts
  from: '@ingydotnet'
  tags: whitespace
  fail: true
  yaml: |
    foo: |
    ————»
    bar: 1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo

- yaml: |
    foo: |
     ———»
    bar: 1
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :foo
       =VAL |\t\n
       =VAL :bar
       =VAL :1
      -MAP
     -DOC
    -STR
  json: |
    {
      "foo": "\t\n",
      "bar": 1
    }
  dump: |
    foo: |
      ———»
    bar: 1

- yaml: |
    - [
    ————»
     foo
     ]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        =VAL :foo
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      [
        "foo"
      ]
    ]
  dump: |
    - - foo

- fail: true
  yaml: |
    - [
    ————»foo,
     foo
     ]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
  json: null

- fail: true
  yaml: |
    -———»-

- fail: true
  yaml: |
    - ——»-

- fail: true
  yaml: |
    ?———»-

- fail: true
  yaml: |
    ? -
    :———»-

- fail: true
  yaml: |
    ?———»key:

- fail: true
  yaml: |
    ? key:
    :———»key:

- yaml: |
    -———»-1
  tree: |
    +STR
     +DOC
      +SEQ
       =VAL :-1
      -SEQ
     -DOC
    -STR
  json: |
    [
      -1
    ]
  dump: |
    - -1
)RAW";

static constexpr std::string_view T_YD5X = R"RAW(
---
- name: Spec Example 2.5. Sequence of Sequences
  from: http://www.yaml.org/spec/1.2/spec.html#id2760351
  tags: spec sequence
  yaml: |
    - [name        , hr, avg  ]
    - [Mark McGwire, 65, 0.278]
    - [Sammy Sosa  , 63, 0.288]
  tree: |
    +STR
     +DOC
      +SEQ
       +SEQ []
        =VAL :name
        =VAL :hr
        =VAL :avg
       -SEQ
       +SEQ []
        =VAL :Mark McGwire
        =VAL :65
        =VAL :0.278
       -SEQ
       +SEQ []
        =VAL :Sammy Sosa
        =VAL :63
        =VAL :0.288
       -SEQ
      -SEQ
     -DOC
    -STR
  json: |
    [
      [
        "name",
        "hr",
        "avg"
      ],
      [
        "Mark McGwire",
        65,
        0.278
      ],
      [
        "Sammy Sosa",
        63,
        0.288
      ]
    ]
  dump: |
    - - name
      - hr
      - avg
    - - Mark McGwire
      - 65
      - 0.278
    - - Sammy Sosa
      - 63
      - 0.288
)RAW";

static constexpr std::string_view T_YJV2 = R"RAW(
---
- name: Dash in flow sequence
  from: '@ingydotnet'
  tags: flow sequence
  fail: true
  yaml: |
    [-]
  tree: |
    +STR
     +DOC
      +SEQ []
)RAW";

static constexpr std::string_view T_Z67P = R"RAW(
---
- name: Spec Example 8.21. Block Scalar Nodes [1.3]
  from: M5C3, modified for YAML 1.3
  tags: indent spec literal folded tag local-tag 1.3-mod
  yaml: |
    literal: |2
      value
    folded: !foo >1
     value
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :literal
       =VAL |value\n
       =VAL :folded
       =VAL <!foo> >value\n
      -MAP
     -DOC
    -STR
  json: |
    {
      "literal": "value\n",
      "folded": "value\n"
    }
  dump: |
    literal: |
      value
    folded: !foo >
      value
)RAW";

static constexpr std::string_view T_Z9M4 = R"RAW(
---
- name: Spec Example 6.22. Global Tag Prefix
  from: http://www.yaml.org/spec/1.2/spec.html#id2783726
  tags: spec header tag unknown-tag
  yaml: |
    %TAG !e! tag:example.com,2000:app/
    ---
    - !e!foo "bar"
  tree: |
    +STR
     +DOC ---
      +SEQ
       =VAL <tag:example.com,2000:app/foo> "bar
      -SEQ
     -DOC
    -STR
  json: |
    [
      "bar"
    ]
  dump: |
    ---
    - !<tag:example.com,2000:app/foo> "bar"
)RAW";

static constexpr std::string_view T_ZCZ6 = R"RAW(
---
- name: Invalid mapping in plain single line value
  from: https://gist.github.com/anonymous/0c8db51d151baf8113205ba3ce71d1b4 via @ingydotnet
  tags: error mapping scalar
  fail: true
  yaml: |
    a: b: c: d
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :a
)RAW";

static constexpr std::string_view T_ZF4X = R"RAW(
---
- name: Spec Example 2.6. Mapping of Mappings
  from: http://www.yaml.org/spec/1.2/spec.html#id2760372
  tags: flow spec mapping
  yaml: |
    Mark McGwire: {hr: 65, avg: 0.278}
    Sammy Sosa: {
        hr: 63,
        avg: 0.288
      }
  tree: |
    +STR
     +DOC
      +MAP
       =VAL :Mark McGwire
       +MAP {}
        =VAL :hr
        =VAL :65
        =VAL :avg
        =VAL :0.278
       -MAP
       =VAL :Sammy Sosa
       +MAP {}
        =VAL :hr
        =VAL :63
        =VAL :avg
        =VAL :0.288
       -MAP
      -MAP
     -DOC
    -STR
  json: |
    {
      "Mark McGwire": {
        "hr": 65,
        "avg": 0.278
      },
      "Sammy Sosa": {
        "hr": 63,
        "avg": 0.288
      }
    }
  dump: |
    Mark McGwire:
      hr: 65
      avg: 0.278
    Sammy Sosa:
      hr: 63
      avg: 0.288
)RAW";

static constexpr std::string_view T_ZH7C = R"RAW(
---
- name: Anchors in Mapping
  from: NimYAML tests
  tags: anchor mapping
  yaml: |
    &a a: b
    c: &d d
  tree: |
    +STR
     +DOC
      +MAP
       =VAL &a :a
       =VAL :b
       =VAL :c
       =VAL &d :d
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": "b",
      "c": "d"
    }
)RAW";

static constexpr std::string_view T_ZK9H = R"RAW(
---
- name: Nested top level flow mapping
  from: '@perlpunk'
  tags: flow indent mapping sequence
  yaml: |
    { key: [[[
      value
     ]]]
    }
  tree: |
    +STR
     +DOC
      +MAP {}
       =VAL :key
       +SEQ []
        +SEQ []
         +SEQ []
          =VAL :value
         -SEQ
        -SEQ
       -SEQ
      -MAP
     -DOC
    -STR
  json: |
    {
      "key": [
        [
          [
            "value"
          ]
        ]
      ]
    }
  dump: |
    key:
    - - - value
)RAW";

static constexpr std::string_view T_ZL4Z = R"RAW(
---
- name: Invalid nested mapping
  from: '@perlpunk'
  tags: error mapping
  fail: true
  yaml: |
    ---
    a: 'b': c
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
       =VAL 'b
)RAW";

static constexpr std::string_view T_ZVH3 = R"RAW(
---
- name: Wrong indented sequence item
  from: '@perlpunk'
  tags: error sequence indent
  fail: true
  yaml: |
    - key: value
     - item1
  tree: |
    +STR
     +DOC
      +SEQ
       +MAP
        =VAL :key
        =VAL :value
       -MAP
)RAW";

static constexpr std::string_view T_ZWK4 = R"RAW(
---
- name: Key with anchor after missing explicit mapping value
  from: '@perlpunk'
  tags: anchor explicit-key mapping
  yaml: |
    ---
    a: 1
    ? b
    &anchor c: 3
  tree: |
    +STR
     +DOC ---
      +MAP
       =VAL :a
       =VAL :1
       =VAL :b
       =VAL :
       =VAL &anchor :c
       =VAL :3
      -MAP
     -DOC
    -STR
  json: |
    {
      "a": 1,
      "b": null,
      "c": 3
    }
  dump: |
    ---
    a: 1
    b:
    &anchor c: 3
)RAW";

static constexpr std::string_view T_ZXT5 = R"RAW(
---
- name: Implicit key followed by newline and adjacent value
  from: '@perlpunk'
  tags: error flow mapping sequence
  fail: true
  yaml: |
    [ "key"
      :value ]
  tree: |
    +STR
     +DOC
      +SEQ []
       =VAL "key
)RAW";

static constexpr std::string_view T_ZYU8 = R"RAW(
---
- skip: true
  note: The following cases are valid YAML according to the 1.2 productions but
    not at all usefully valid. We don't want to encourage parsers to support
    them when we'll likely make them invalid later.
  also: MU58
  name: Directive variants
  from: '@ingydotnet'
  tags: directive
  yaml: |
    %YAML1.1
    ---
  tree: |
    +STR
     +DOC ---
      =VAL :
     -DOC
    -STR
  json: |
    null

- yaml: |
    %***
    ---

- yaml: |
    %YAML 1.1 1.2
    ---

- yaml: |
    %YAML 1.12345
    ---
)RAW";



namespace nix {
    TEST_F(FromYAMLTest, T_229Q) {
        ASSERT_EQ(execYAMLTest(T_229Q),"OK");
    }

    TEST_F(FromYAMLTest, T_236B) {
        ASSERT_EQ(execYAMLTest(T_236B),"OK");
    }

    TEST_F(FromYAMLTest, T_26DV) {
        ASSERT_EQ(execYAMLTest(T_26DV),"OK");
    }

    TEST_F(FromYAMLTest, T_27NA) {
        ASSERT_EQ(execYAMLTest(T_27NA),"OK");
    }

    TEST_F(FromYAMLTest, T_2AUY) {
        ASSERT_EQ(execYAMLTest(T_2AUY),"OK");
    }

    TEST_F(FromYAMLTest, T_2CMS) {
        ASSERT_EQ(execYAMLTest(T_2CMS),"OK");
    }

    TEST_F(FromYAMLTest, T_2EBW) {
        ASSERT_EQ(execYAMLTest(T_2EBW),"OK");
    }

    TEST_F(FromYAMLTest, T_2G84) {
        ASSERT_EQ(execYAMLTest(T_2G84),"OK");
    }

    TEST_F(FromYAMLTest, T_2JQS) {
        ASSERT_EQ(execYAMLTest(T_2JQS),"OK");
    }

    TEST_F(FromYAMLTest, T_2LFX) {
        ASSERT_EQ(execYAMLTest(T_2LFX),"OK");
    }

    TEST_F(FromYAMLTest, T_2SXE) {
        ASSERT_EQ(execYAMLTest(T_2SXE),"OK");
    }

    TEST_F(FromYAMLTest, T_2XXW) {
        ASSERT_EQ(execYAMLTest(T_2XXW),"OK");
    }

    TEST_F(FromYAMLTest, T_33X3) {
        ASSERT_EQ(execYAMLTest(T_33X3),"OK");
    }

    TEST_F(FromYAMLTest, T_35KP) {
        ASSERT_EQ(execYAMLTest(T_35KP),"OK");
    }

    TEST_F(FromYAMLTest, T_36F6) {
        ASSERT_EQ(execYAMLTest(T_36F6),"OK");
    }

    TEST_F(FromYAMLTest, T_3ALJ) {
        ASSERT_EQ(execYAMLTest(T_3ALJ),"OK");
    }

    TEST_F(FromYAMLTest, T_3GZX) {
        ASSERT_EQ(execYAMLTest(T_3GZX),"OK");
    }

    TEST_F(FromYAMLTest, T_3HFZ) {
        ASSERT_EQ(execYAMLTest(T_3HFZ),"OK");
    }

    TEST_F(FromYAMLTest, T_3MYT) {
        ASSERT_EQ(execYAMLTest(T_3MYT),"OK");
    }

    TEST_F(FromYAMLTest, T_3R3P) {
        ASSERT_EQ(execYAMLTest(T_3R3P),"OK");
    }

    TEST_F(FromYAMLTest, T_3RLN) {
        ASSERT_EQ(execYAMLTest(T_3RLN),"OK");
    }

    TEST_F(FromYAMLTest, T_3UYS) {
        ASSERT_EQ(execYAMLTest(T_3UYS),"OK");
    }

    TEST_F(FromYAMLTest, T_4ABK) {
        ASSERT_EQ(execYAMLTest(T_4ABK),"OK");
    }

    TEST_F(FromYAMLTest, T_4CQQ) {
        ASSERT_EQ(execYAMLTest(T_4CQQ),"OK");
    }

    TEST_F(FromYAMLTest, T_4EJS) {
        ASSERT_EQ(execYAMLTest(T_4EJS),"OK");
    }

    TEST_F(FromYAMLTest, T_4FJ6) {
        ASSERT_EQ(execYAMLTest(T_4FJ6),"OK");
    }

    TEST_F(FromYAMLTest, T_4GC6) {
        ASSERT_EQ(execYAMLTest(T_4GC6),"OK");
    }

    TEST_F(FromYAMLTest, T_4H7K) {
        ASSERT_EQ(execYAMLTest(T_4H7K),"OK");
    }

    TEST_F(FromYAMLTest, T_4HVU) {
        ASSERT_EQ(execYAMLTest(T_4HVU),"OK");
    }

    TEST_F(FromYAMLTest, T_4JVG) {
        ASSERT_EQ(execYAMLTest(T_4JVG),"OK");
    }

    TEST_F(FromYAMLTest, T_4MUZ) {
        ASSERT_EQ(execYAMLTest(T_4MUZ),"OK");
    }

    TEST_F(FromYAMLTest, T_4Q9F) {
        ASSERT_EQ(execYAMLTest(T_4Q9F),"OK");
    }

    TEST_F(FromYAMLTest, T_4QFQ) {
        ASSERT_EQ(execYAMLTest(T_4QFQ),"OK");
    }

    TEST_F(FromYAMLTest, T_4RWC) {
        ASSERT_EQ(execYAMLTest(T_4RWC),"OK");
    }

    TEST_F(FromYAMLTest, T_4UYU) {
        ASSERT_EQ(execYAMLTest(T_4UYU),"OK");
    }

    TEST_F(FromYAMLTest, T_4V8U) {
        ASSERT_EQ(execYAMLTest(T_4V8U),"OK");
    }

    TEST_F(FromYAMLTest, T_4WA9) {
        ASSERT_EQ(execYAMLTest(T_4WA9),"OK");
    }

    TEST_F(FromYAMLTest, T_4ZYM) {
        ASSERT_EQ(execYAMLTest(T_4ZYM),"OK");
    }

    TEST_F(FromYAMLTest, T_52DL) {
        ASSERT_EQ(execYAMLTest(T_52DL),"OK");
    }

    TEST_F(FromYAMLTest, T_54T7) {
        ASSERT_EQ(execYAMLTest(T_54T7),"OK");
    }

    TEST_F(FromYAMLTest, T_55WF) {
        ASSERT_EQ(execYAMLTest(T_55WF),"OK");
    }

    TEST_F(FromYAMLTest, T_565N) {
        ASSERT_THROW(execYAMLTest(T_565N),EvalError); // nix has no binary data type
    }

    TEST_F(FromYAMLTest, T_57H4) {
        ASSERT_EQ(execYAMLTest(T_57H4),"OK");
    }

    TEST_F(FromYAMLTest, T_58MP) {
        ASSERT_EQ(execYAMLTest(T_58MP),"OK");
    }

    TEST_F(FromYAMLTest, T_5BVJ) {
        ASSERT_EQ(execYAMLTest(T_5BVJ),"OK");
    }

    TEST_F(FromYAMLTest, T_5C5M) {
        ASSERT_EQ(execYAMLTest(T_5C5M),"OK");
    }

    TEST_F(FromYAMLTest, T_5GBF) {
        ASSERT_EQ(execYAMLTest(T_5GBF),"OK");
    }

    TEST_F(FromYAMLTest, T_5KJE) {
        ASSERT_EQ(execYAMLTest(T_5KJE),"OK");
    }

    TEST_F(FromYAMLTest, T_5LLU) {
        ASSERT_EQ(execYAMLTest(T_5LLU),"OK");
    }

    TEST_F(FromYAMLTest, T_5MUD) {
        ASSERT_EQ(execYAMLTest(T_5MUD),"OK");
    }

    TEST_F(FromYAMLTest, T_5NYZ) {
        ASSERT_EQ(execYAMLTest(T_5NYZ),"OK");
    }

    TEST_F(FromYAMLTest, T_5T43) {
        ASSERT_EQ(execYAMLTest(T_5T43),"OK");
    }

    TEST_F(FromYAMLTest, T_5TRB) {
        ASSERT_EQ(execYAMLTest(T_5TRB),"OK");
    }

    TEST_F(FromYAMLTest, T_5TYM) {
        ASSERT_EQ(execYAMLTest(T_5TYM),"OK");
    }

    TEST_F(FromYAMLTest, T_5U3A) {
        ASSERT_EQ(execYAMLTest(T_5U3A),"OK");
    }

    TEST_F(FromYAMLTest, T_5WE3) {
        ASSERT_EQ(execYAMLTest(T_5WE3),"OK");
    }

    TEST_F(FromYAMLTest, T_62EZ) {
        ASSERT_EQ(execYAMLTest(T_62EZ),"OK");
    }

    TEST_F(FromYAMLTest, T_652Z) {
        ASSERT_EQ(execYAMLTest(T_652Z),"OK");
    }

    TEST_F(FromYAMLTest, T_65WH) {
        ASSERT_EQ(execYAMLTest(T_65WH),"OK");
    }

    TEST_F(FromYAMLTest, T_6BCT) {
        ASSERT_EQ(execYAMLTest(T_6BCT),"OK");
    }

    TEST_F(FromYAMLTest, T_6BFJ) {
        ASSERT_EQ(execYAMLTest(T_6BFJ),"OK");
    }

    TEST_F(FromYAMLTest, T_6CA3) {
        ASSERT_EQ(execYAMLTest(T_6CA3),"OK");
    }

    TEST_F(FromYAMLTest, T_6CK3) {
        ASSERT_EQ(execYAMLTest(T_6CK3),"OK");
    }

    TEST_F(FromYAMLTest, T_6FWR) {
        ASSERT_EQ(execYAMLTest(T_6FWR),"OK");
    }

    TEST_F(FromYAMLTest, T_6H3V) {
        ASSERT_EQ(execYAMLTest(T_6H3V),"OK");
    }

    TEST_F(FromYAMLTest, T_6HB6) {
        ASSERT_EQ(execYAMLTest(T_6HB6),"OK");
    }

    TEST_F(FromYAMLTest, T_6JQW) {
        ASSERT_EQ(execYAMLTest(T_6JQW),"OK");
    }

    TEST_F(FromYAMLTest, T_6JTT) {
        ASSERT_EQ(execYAMLTest(T_6JTT),"OK");
    }

    TEST_F(FromYAMLTest, T_6JWB) {
        ASSERT_EQ(execYAMLTest(T_6JWB),"OK");
    }

    TEST_F(FromYAMLTest, T_6KGN) {
        ASSERT_EQ(execYAMLTest(T_6KGN),"OK");
    }

    TEST_F(FromYAMLTest, T_6LVF) {
        ASSERT_EQ(execYAMLTest(T_6LVF),"OK");
    }

    TEST_F(FromYAMLTest, T_6M2F) {
        ASSERT_EQ(execYAMLTest(T_6M2F),"OK");
    }

    TEST_F(FromYAMLTest, T_6PBE) {
        ASSERT_EQ(execYAMLTest(T_6PBE),"OK");
    }

    TEST_F(FromYAMLTest, T_6S55) {
        ASSERT_EQ(execYAMLTest(T_6S55),"OK");
    }

    TEST_F(FromYAMLTest, T_6SLA) {
        ASSERT_EQ(execYAMLTest(T_6SLA),"OK");
    }

    TEST_F(FromYAMLTest, T_6VJK) {
        ASSERT_EQ(execYAMLTest(T_6VJK),"OK");
    }

    TEST_F(FromYAMLTest, T_6WLZ) {
        ASSERT_EQ(execYAMLTest(T_6WLZ),"OK");
    }

    TEST_F(FromYAMLTest, T_6WPF) {
        ASSERT_EQ(execYAMLTest(T_6WPF),"OK");
    }

    TEST_F(FromYAMLTest, T_6XDY) {
        ASSERT_EQ(execYAMLTest(T_6XDY),"OK");
    }

    TEST_F(FromYAMLTest, T_6ZKB) {
        ASSERT_EQ(execYAMLTest(T_6ZKB),"OK");
    }

    TEST_F(FromYAMLTest, T_735Y) {
        ASSERT_EQ(execYAMLTest(T_735Y),"OK");
    }

    TEST_F(FromYAMLTest, T_74H7) {
        ASSERT_EQ(execYAMLTest(T_74H7),"OK");
    }

    TEST_F(FromYAMLTest, T_753E) {
        ASSERT_EQ(execYAMLTest(T_753E),"OK");
    }

    TEST_F(FromYAMLTest, T_7A4E) {
        ASSERT_EQ(execYAMLTest(T_7A4E),"OK");
    }

    TEST_F(FromYAMLTest, T_7BMT) {
        ASSERT_EQ(execYAMLTest(T_7BMT),"OK");
    }

    TEST_F(FromYAMLTest, T_7BUB) {
        ASSERT_EQ(execYAMLTest(T_7BUB),"OK");
    }

    TEST_F(FromYAMLTest, T_7FWL) {
        ASSERT_EQ(execYAMLTest(T_7FWL),"OK");
    }

    TEST_F(FromYAMLTest, T_7LBH) {
        ASSERT_EQ(execYAMLTest(T_7LBH),"OK");
    }

    TEST_F(FromYAMLTest, T_7MNF) {
        ASSERT_EQ(execYAMLTest(T_7MNF),"OK");
    }

    TEST_F(FromYAMLTest, T_7T8X) {
        ASSERT_EQ(execYAMLTest(T_7T8X),"OK");
    }

    TEST_F(FromYAMLTest, T_7TMG) {
        ASSERT_EQ(execYAMLTest(T_7TMG),"OK");
    }

    TEST_F(FromYAMLTest, T_7W2P) {
        ASSERT_EQ(execYAMLTest(T_7W2P),"OK");
    }

    TEST_F(FromYAMLTest, T_7Z25) {
        ASSERT_EQ(execYAMLTest(T_7Z25),"OK");
    }

    TEST_F(FromYAMLTest, T_7ZZ5) {
        ASSERT_EQ(execYAMLTest(T_7ZZ5),"OK");
    }

    TEST_F(FromYAMLTest, T_82AN) {
        ASSERT_EQ(execYAMLTest(T_82AN),"OK");
    }

    TEST_F(FromYAMLTest, T_87E4) {
        ASSERT_EQ(execYAMLTest(T_87E4),"OK");
    }

    TEST_F(FromYAMLTest, T_8CWC) {
        ASSERT_EQ(execYAMLTest(T_8CWC),"OK");
    }

    TEST_F(FromYAMLTest, T_8G76) {
        ASSERT_EQ(execYAMLTest(T_8G76),"OK");
    }

    TEST_F(FromYAMLTest, T_8KB6) {
        ASSERT_EQ(execYAMLTest(T_8KB6),"OK");
    }

    TEST_F(FromYAMLTest, T_8MK2) {
        ASSERT_EQ(execYAMLTest(T_8MK2),"OK");
    }

    TEST_F(FromYAMLTest, T_8QBE) {
        ASSERT_EQ(execYAMLTest(T_8QBE),"OK");
    }

    TEST_F(FromYAMLTest, T_8UDB) {
        ASSERT_EQ(execYAMLTest(T_8UDB),"OK");
    }

    TEST_F(FromYAMLTest, T_8XDJ) {
        ASSERT_EQ(execYAMLTest(T_8XDJ),"OK");
    }

    TEST_F(FromYAMLTest, T_8XYN) {
        ASSERT_EQ(execYAMLTest(T_8XYN),"OK");
    }

    TEST_F(FromYAMLTest, T_93JH) {
        ASSERT_EQ(execYAMLTest(T_93JH),"OK");
    }

    TEST_F(FromYAMLTest, T_93WF) {
        ASSERT_EQ(execYAMLTest(T_93WF),"OK");
    }

    TEST_F(FromYAMLTest, T_96L6) {
        ASSERT_EQ(execYAMLTest(T_96L6),"OK");
    }

    TEST_F(FromYAMLTest, T_96NN) {
        ASSERT_EQ(execYAMLTest(T_96NN),"OK");
    }

    TEST_F(FromYAMLTest, T_98YD) {
        ASSERT_EQ(execYAMLTest(T_98YD),"OK");
    }

    TEST_F(FromYAMLTest, T_9BXH) {
        ASSERT_EQ(execYAMLTest(T_9BXH),"OK");
    }

    TEST_F(FromYAMLTest, T_9C9N) {
        ASSERT_EQ(execYAMLTest(T_9C9N),"OK");
    }

    TEST_F(FromYAMLTest, T_9CWY) {
        ASSERT_EQ(execYAMLTest(T_9CWY),"OK");
    }

    TEST_F(FromYAMLTest, T_9DXL) {
        ASSERT_EQ(execYAMLTest(T_9DXL),"OK");
    }

    TEST_F(FromYAMLTest, T_9FMG) {
        ASSERT_EQ(execYAMLTest(T_9FMG),"OK");
    }

    TEST_F(FromYAMLTest, T_9HCY) {
        ASSERT_EQ(execYAMLTest(T_9HCY),"OK");
    }

    TEST_F(FromYAMLTest, T_9J7A) {
        ASSERT_EQ(execYAMLTest(T_9J7A),"OK");
    }

    TEST_F(FromYAMLTest, T_9JBA) {
        ASSERT_EQ(execYAMLTest(T_9JBA),"OK");
    }

    TEST_F(FromYAMLTest, T_9KAX) {
        ASSERT_EQ(execYAMLTest(T_9KAX),"OK");
    }

    TEST_F(FromYAMLTest, T_9KBC) {
        ASSERT_EQ(execYAMLTest(T_9KBC),"OK");
    }

    TEST_F(FromYAMLTest, T_9MAG) {
        ASSERT_EQ(execYAMLTest(T_9MAG),"OK");
    }

    TEST_F(FromYAMLTest, T_9MMA) {
        ASSERT_EQ(execYAMLTest(T_9MMA),"OK");
    }

    TEST_F(FromYAMLTest, T_9MMW) {
        ASSERT_EQ(execYAMLTest(T_9MMW),"OK");
    }

    TEST_F(FromYAMLTest, T_9MQT) {
        ASSERT_EQ(execYAMLTest(T_9MQT),"OK");
    }

    TEST_F(FromYAMLTest, T_9SA2) {
        ASSERT_EQ(execYAMLTest(T_9SA2),"OK");
    }

    TEST_F(FromYAMLTest, T_9SHH) {
        ASSERT_EQ(execYAMLTest(T_9SHH),"OK");
    }

    TEST_F(FromYAMLTest, T_9TFX) {
        ASSERT_EQ(execYAMLTest(T_9TFX),"OK");
    }

    TEST_F(FromYAMLTest, T_9U5K) {
        ASSERT_EQ(execYAMLTest(T_9U5K),"OK");
    }

    TEST_F(FromYAMLTest, T_9WXW) {
        ASSERT_EQ(execYAMLTest(T_9WXW),"OK");
    }

    TEST_F(FromYAMLTest, T_9YRD) {
        ASSERT_EQ(execYAMLTest(T_9YRD),"OK");
    }

    TEST_F(FromYAMLTest, T_A2M4) {
        ASSERT_EQ(execYAMLTest(T_A2M4),"OK");
    }

    TEST_F(FromYAMLTest, T_A6F9) {
        ASSERT_EQ(execYAMLTest(T_A6F9),"OK");
    }

    TEST_F(FromYAMLTest, T_A984) {
        ASSERT_EQ(execYAMLTest(T_A984),"OK");
    }

    TEST_F(FromYAMLTest, T_AB8U) {
        ASSERT_EQ(execYAMLTest(T_AB8U),"OK");
    }

    TEST_F(FromYAMLTest, T_AVM7) {
        ASSERT_EQ(execYAMLTest(T_AVM7),"OK");
    }

    TEST_F(FromYAMLTest, T_AZ63) {
        ASSERT_EQ(execYAMLTest(T_AZ63),"OK");
    }

    TEST_F(FromYAMLTest, T_AZW3) {
        ASSERT_EQ(execYAMLTest(T_AZW3),"OK");
    }

    TEST_F(FromYAMLTest, T_B3HG) {
        ASSERT_EQ(execYAMLTest(T_B3HG),"OK");
    }

    TEST_F(FromYAMLTest, T_B63P) {
        ASSERT_EQ(execYAMLTest(T_B63P),"OK");
    }

    TEST_F(FromYAMLTest, T_BD7L) {
        ASSERT_EQ(execYAMLTest(T_BD7L),"OK");
    }

    TEST_F(FromYAMLTest, T_BEC7) {
        ASSERT_EQ(execYAMLTest(T_BEC7),"OK");
    }

    TEST_F(FromYAMLTest, T_BF9H) {
        ASSERT_EQ(execYAMLTest(T_BF9H),"OK");
    }

    TEST_F(FromYAMLTest, T_BS4K) {
        ASSERT_EQ(execYAMLTest(T_BS4K),"OK");
    }

    TEST_F(FromYAMLTest, T_BU8L) {
        ASSERT_EQ(execYAMLTest(T_BU8L),"OK");
    }

    TEST_F(FromYAMLTest, T_C2DT) {
        ASSERT_EQ(execYAMLTest(T_C2DT),"OK");
    }

    TEST_F(FromYAMLTest, T_C2SP) {
        ASSERT_EQ(execYAMLTest(T_C2SP),"OK");
    }

    TEST_F(FromYAMLTest, T_C4HZ) {
        ASSERT_EQ(execYAMLTest(T_C4HZ),"OK");
    }

    TEST_F(FromYAMLTest, T_CC74) {
        ASSERT_EQ(execYAMLTest(T_CC74),"OK");
    }

    TEST_F(FromYAMLTest, T_CFD4) {
        ASSERT_EQ(execYAMLTest(T_CFD4),"OK");
    }

    TEST_F(FromYAMLTest, T_CML9) {
        ASSERT_EQ(execYAMLTest(T_CML9),"OK");
    }

    TEST_F(FromYAMLTest, T_CN3R) {
        ASSERT_EQ(execYAMLTest(T_CN3R),"OK");
    }

    TEST_F(FromYAMLTest, T_CPZ3) {
        ASSERT_EQ(execYAMLTest(T_CPZ3),"OK");
    }

    TEST_F(FromYAMLTest, T_CQ3W) {
        ASSERT_EQ(execYAMLTest(T_CQ3W),"OK");
    }

    TEST_F(FromYAMLTest, T_CT4Q) {
        ASSERT_EQ(execYAMLTest(T_CT4Q),"OK");
    }

    TEST_F(FromYAMLTest, T_CTN5) {
        ASSERT_EQ(execYAMLTest(T_CTN5),"OK");
    }

    TEST_F(FromYAMLTest, T_CUP7) {
        ASSERT_EQ(execYAMLTest(T_CUP7),"OK");
    }

    TEST_F(FromYAMLTest, T_CVW2) {
        ASSERT_EQ(execYAMLTest(T_CVW2),"OK");
    }

    TEST_F(FromYAMLTest, T_CXX2) {
        ASSERT_EQ(execYAMLTest(T_CXX2),"OK");
    }

    TEST_F(FromYAMLTest, T_D49Q) {
        ASSERT_EQ(execYAMLTest(T_D49Q),"OK");
    }

    TEST_F(FromYAMLTest, T_D83L) {
        ASSERT_EQ(execYAMLTest(T_D83L),"OK");
    }

    TEST_F(FromYAMLTest, T_D88J) {
        ASSERT_EQ(execYAMLTest(T_D88J),"OK");
    }

    TEST_F(FromYAMLTest, T_D9TU) {
        ASSERT_EQ(execYAMLTest(T_D9TU),"OK");
    }

    TEST_F(FromYAMLTest, T_DBG4) {
        ASSERT_EQ(execYAMLTest(T_DBG4),"OK");
    }

    TEST_F(FromYAMLTest, T_DC7X) {
        ASSERT_EQ(execYAMLTest(T_DC7X),"OK");
    }

    TEST_F(FromYAMLTest, T_DE56) {
        ASSERT_EQ(execYAMLTest(T_DE56),"OK");
    }

    TEST_F(FromYAMLTest, T_DFF7) {
        ASSERT_EQ(execYAMLTest(T_DFF7),"OK");
    }

    TEST_F(FromYAMLTest, T_DHP8) {
        ASSERT_EQ(execYAMLTest(T_DHP8),"OK");
    }

    TEST_F(FromYAMLTest, T_DK3J) {
        ASSERT_EQ(execYAMLTest(T_DK3J),"OK");
    }

    TEST_F(FromYAMLTest, T_DK4H) {
        ASSERT_EQ(execYAMLTest(T_DK4H),"OK");
    }

    TEST_F(FromYAMLTest, T_DK95) {
        ASSERT_EQ(execYAMLTest(T_DK95),"OK");
    }

    TEST_F(FromYAMLTest, T_DMG6) {
        ASSERT_EQ(execYAMLTest(T_DMG6),"OK");
    }

    TEST_F(FromYAMLTest, T_DWX9) {
        ASSERT_EQ(execYAMLTest(T_DWX9),"OK");
    }

    TEST_F(FromYAMLTest, T_E76Z) {
        ASSERT_EQ(execYAMLTest(T_E76Z),"OK");
    }

    TEST_F(FromYAMLTest, T_EB22) {
        ASSERT_EQ(execYAMLTest(T_EB22),"OK");
    }

    TEST_F(FromYAMLTest, T_EHF6) {
        ASSERT_EQ(execYAMLTest(T_EHF6),"OK");
    }

    TEST_F(FromYAMLTest, T_EW3V) {
        ASSERT_EQ(execYAMLTest(T_EW3V),"OK");
    }

    TEST_F(FromYAMLTest, T_EX5H) {
        ASSERT_EQ(execYAMLTest(T_EX5H),"OK");
    }

    TEST_F(FromYAMLTest, T_EXG3) {
        ASSERT_EQ(execYAMLTest(T_EXG3),"OK");
    }

    TEST_F(FromYAMLTest, T_F2C7) {
        ASSERT_EQ(execYAMLTest(T_F2C7),"OK");
    }

    TEST_F(FromYAMLTest, T_F3CP) {
        ASSERT_EQ(execYAMLTest(T_F3CP),"OK");
    }

    TEST_F(FromYAMLTest, T_F6MC) {
        ASSERT_EQ(execYAMLTest(T_F6MC),"OK");
    }

    TEST_F(FromYAMLTest, T_F8F9) {
        ASSERT_EQ(execYAMLTest(T_F8F9),"OK");
    }

    TEST_F(FromYAMLTest, T_FBC9) {
        ASSERT_EQ(execYAMLTest(T_FBC9),"OK");
    }

    TEST_F(FromYAMLTest, T_FH7J) {
        ASSERT_EQ(execYAMLTest(T_FH7J),"OK");
    }

    TEST_F(FromYAMLTest, T_FP8R) {
        ASSERT_EQ(execYAMLTest(T_FP8R),"OK");
    }

    TEST_F(FromYAMLTest, T_FQ7F) {
        ASSERT_EQ(execYAMLTest(T_FQ7F),"OK");
    }

    TEST_F(FromYAMLTest, T_FRK4) {
        ASSERT_EQ(execYAMLTest(T_FRK4),"OK");
    }

    TEST_F(FromYAMLTest, T_FTA2) {
        ASSERT_EQ(execYAMLTest(T_FTA2),"OK");
    }

    TEST_F(FromYAMLTest, T_FUP4) {
        ASSERT_EQ(execYAMLTest(T_FUP4),"OK");
    }

    TEST_F(FromYAMLTest, T_G4RS) {
        ASSERT_EQ(execYAMLTest(T_G4RS),"OK");
    }

    TEST_F(FromYAMLTest, T_G5U8) {
        ASSERT_EQ(execYAMLTest(T_G5U8),"OK");
    }

    TEST_F(FromYAMLTest, T_G7JE) {
        ASSERT_EQ(execYAMLTest(T_G7JE),"OK");
    }

    TEST_F(FromYAMLTest, T_G992) {
        ASSERT_EQ(execYAMLTest(T_G992),"OK");
    }

    TEST_F(FromYAMLTest, T_G9HC) {
        ASSERT_EQ(execYAMLTest(T_G9HC),"OK");
    }

    TEST_F(FromYAMLTest, T_GDY7) {
        ASSERT_EQ(execYAMLTest(T_GDY7),"OK");
    }

    TEST_F(FromYAMLTest, T_GH63) {
        ASSERT_EQ(execYAMLTest(T_GH63),"OK");
    }

    TEST_F(FromYAMLTest, T_GT5M) {
        ASSERT_EQ(execYAMLTest(T_GT5M),"OK");
    }

    TEST_F(FromYAMLTest, T_H2RW) {
        ASSERT_EQ(execYAMLTest(T_H2RW),"OK");
    }

    TEST_F(FromYAMLTest, T_H3Z8) {
        ASSERT_EQ(execYAMLTest(T_H3Z8),"OK");
    }

    TEST_F(FromYAMLTest, T_H7J7) {
        ASSERT_EQ(execYAMLTest(T_H7J7),"OK");
    }

    TEST_F(FromYAMLTest, T_H7TQ) {
        ASSERT_EQ(execYAMLTest(T_H7TQ),"OK");
    }

    TEST_F(FromYAMLTest, T_HM87) {
        ASSERT_EQ(execYAMLTest(T_HM87),"OK");
    }

    TEST_F(FromYAMLTest, T_HMK4) {
        ASSERT_EQ(execYAMLTest(T_HMK4),"OK");
    }

    TEST_F(FromYAMLTest, T_HMQ5) {
        ASSERT_EQ(execYAMLTest(T_HMQ5),"OK");
    }

    TEST_F(FromYAMLTest, T_HRE5) {
        ASSERT_EQ(execYAMLTest(T_HRE5),"OK");
    }

    TEST_F(FromYAMLTest, T_HS5T) {
        ASSERT_EQ(execYAMLTest(T_HS5T),"OK");
    }

    TEST_F(FromYAMLTest, T_HU3P) {
        ASSERT_EQ(execYAMLTest(T_HU3P),"OK");
    }

    TEST_F(FromYAMLTest, T_HWV9) {
        ASSERT_EQ(execYAMLTest(T_HWV9),"OK");
    }

    TEST_F(FromYAMLTest, T_J3BT) {
        ASSERT_EQ(execYAMLTest(T_J3BT),"OK");
    }

    TEST_F(FromYAMLTest, T_J5UC) {
        ASSERT_EQ(execYAMLTest(T_J5UC),"OK");
    }

    TEST_F(FromYAMLTest, T_J7PZ) {
        ASSERT_EQ(execYAMLTest(T_J7PZ),"OK");
    }

    TEST_F(FromYAMLTest, T_J7VC) {
        ASSERT_EQ(execYAMLTest(T_J7VC),"OK");
    }

    TEST_F(FromYAMLTest, T_J9HZ) {
        ASSERT_EQ(execYAMLTest(T_J9HZ),"OK");
    }

    TEST_F(FromYAMLTest, T_JEF9) {
        ASSERT_EQ(execYAMLTest(T_JEF9),"OK");
    }

    TEST_F(FromYAMLTest, T_JHB9) {
        ASSERT_EQ(execYAMLTest(T_JHB9),"OK");
    }

    TEST_F(FromYAMLTest, T_JKF3) {
        ASSERT_EQ(execYAMLTest(T_JKF3),"OK");
    }

    TEST_F(FromYAMLTest, T_JQ4R) {
        ASSERT_EQ(execYAMLTest(T_JQ4R),"OK");
    }

    TEST_F(FromYAMLTest, T_JR7V) {
        ASSERT_EQ(execYAMLTest(T_JR7V),"OK");
    }

    TEST_F(FromYAMLTest, T_JS2J) {
        ASSERT_EQ(execYAMLTest(T_JS2J),"OK");
    }

    TEST_F(FromYAMLTest, T_JTV5) {
        ASSERT_EQ(execYAMLTest(T_JTV5),"OK");
    }

    TEST_F(FromYAMLTest, T_JY7Z) {
        ASSERT_EQ(execYAMLTest(T_JY7Z),"OK");
    }

    TEST_F(FromYAMLTest, T_K3WX) {
        ASSERT_EQ(execYAMLTest(T_K3WX),"OK");
    }

    TEST_F(FromYAMLTest, T_K4SU) {
        ASSERT_EQ(execYAMLTest(T_K4SU),"OK");
    }

    TEST_F(FromYAMLTest, T_K527) {
        ASSERT_EQ(execYAMLTest(T_K527),"OK");
    }

    TEST_F(FromYAMLTest, T_K54U) {
        ASSERT_EQ(execYAMLTest(T_K54U),"OK");
    }

    TEST_F(FromYAMLTest, T_K858) {
        ASSERT_EQ(execYAMLTest(T_K858),"OK");
    }

    TEST_F(FromYAMLTest, T_KH5V) {
        ASSERT_EQ(execYAMLTest(T_KH5V),"OK");
    }

    TEST_F(FromYAMLTest, T_KK5P) {
        ASSERT_EQ(execYAMLTest(T_KK5P),"OK");
    }

    TEST_F(FromYAMLTest, T_KMK3) {
        ASSERT_EQ(execYAMLTest(T_KMK3),"OK");
    }

    TEST_F(FromYAMLTest, T_KS4U) {
        ASSERT_EQ(execYAMLTest(T_KS4U),"OK");
    }

    TEST_F(FromYAMLTest, T_KSS4) {
        ASSERT_EQ(execYAMLTest(T_KSS4),"OK");
    }

    TEST_F(FromYAMLTest, T_L24T) {
        ASSERT_EQ(execYAMLTest(T_L24T),"OK");
    }

    TEST_F(FromYAMLTest, T_L383) {
        ASSERT_EQ(execYAMLTest(T_L383),"OK");
    }

    TEST_F(FromYAMLTest, T_L94M) {
        ASSERT_EQ(execYAMLTest(T_L94M),"OK");
    }

    TEST_F(FromYAMLTest, T_L9U5) {
        ASSERT_EQ(execYAMLTest(T_L9U5),"OK");
    }

    TEST_F(FromYAMLTest, T_LE5A) {
        ASSERT_EQ(execYAMLTest(T_LE5A),"OK");
    }

    TEST_F(FromYAMLTest, T_LHL4) {
        ASSERT_EQ(execYAMLTest(T_LHL4),"OK");
    }

    TEST_F(FromYAMLTest, T_LP6E) {
        ASSERT_EQ(execYAMLTest(T_LP6E),"OK");
    }

    TEST_F(FromYAMLTest, T_LQZ7) {
        ASSERT_EQ(execYAMLTest(T_LQZ7),"OK");
    }

    TEST_F(FromYAMLTest, T_LX3P) {
        ASSERT_EQ(execYAMLTest(T_LX3P),"OK");
    }

    TEST_F(FromYAMLTest, T_M29M) {
        ASSERT_EQ(execYAMLTest(T_M29M),"OK");
    }

    TEST_F(FromYAMLTest, T_M2N8) {
        ASSERT_EQ(execYAMLTest(T_M2N8),"OK");
    }

    TEST_F(FromYAMLTest, T_M5C3) {
        ASSERT_EQ(execYAMLTest(T_M5C3),"OK");
    }

    TEST_F(FromYAMLTest, T_M5DY) {
        ASSERT_EQ(execYAMLTest(T_M5DY),"OK");
    }

    TEST_F(FromYAMLTest, T_M6YH) {
        ASSERT_EQ(execYAMLTest(T_M6YH),"OK");
    }

    TEST_F(FromYAMLTest, T_M7A3) {
        ASSERT_EQ(execYAMLTest(T_M7A3),"OK");
    }

    TEST_F(FromYAMLTest, T_M7NX) {
        ASSERT_EQ(execYAMLTest(T_M7NX),"OK");
    }

    TEST_F(FromYAMLTest, T_M9B4) {
        ASSERT_EQ(execYAMLTest(T_M9B4),"OK");
    }

    TEST_F(FromYAMLTest, T_MJS9) {
        ASSERT_EQ(execYAMLTest(T_MJS9),"OK");
    }

    TEST_F(FromYAMLTest, T_MUS6) {
        ASSERT_EQ(execYAMLTest(T_MUS6),"OK");
    }

    TEST_F(FromYAMLTest, T_MXS3) {
        ASSERT_EQ(execYAMLTest(T_MXS3),"OK");
    }

    TEST_F(FromYAMLTest, T_MYW6) {
        ASSERT_EQ(execYAMLTest(T_MYW6),"OK");
    }

    TEST_F(FromYAMLTest, T_MZX3) {
        ASSERT_EQ(execYAMLTest(T_MZX3),"OK");
    }

    TEST_F(FromYAMLTest, T_N4JP) {
        ASSERT_EQ(execYAMLTest(T_N4JP),"OK");
    }

    TEST_F(FromYAMLTest, T_N782) {
        ASSERT_EQ(execYAMLTest(T_N782),"OK");
    }

    TEST_F(FromYAMLTest, T_NAT4) {
        ASSERT_EQ(execYAMLTest(T_NAT4),"OK");
    }

    TEST_F(FromYAMLTest, T_NB6Z) {
        ASSERT_EQ(execYAMLTest(T_NB6Z),"OK");
    }

    TEST_F(FromYAMLTest, T_NHX8) {
        ASSERT_EQ(execYAMLTest(T_NHX8),"OK");
    }

    TEST_F(FromYAMLTest, T_NJ66) {
        ASSERT_EQ(execYAMLTest(T_NJ66),"OK");
    }

    TEST_F(FromYAMLTest, T_NKF9) {
        ASSERT_EQ(execYAMLTest(T_NKF9),"OK");
    }

    TEST_F(FromYAMLTest, T_NP9H) {
        ASSERT_EQ(execYAMLTest(T_NP9H),"OK");
    }

    TEST_F(FromYAMLTest, T_P2AD) {
        ASSERT_EQ(execYAMLTest(T_P2AD),"OK");
    }

    TEST_F(FromYAMLTest, T_P2EQ) {
        ASSERT_EQ(execYAMLTest(T_P2EQ),"OK");
    }

    TEST_F(FromYAMLTest, T_P76L) {
        ASSERT_EQ(execYAMLTest(T_P76L),"OK");
    }

    TEST_F(FromYAMLTest, T_P94K) {
        ASSERT_EQ(execYAMLTest(T_P94K),"OK");
    }

    TEST_F(FromYAMLTest, T_PBJ2) {
        ASSERT_EQ(execYAMLTest(T_PBJ2),"OK");
    }

    TEST_F(FromYAMLTest, T_PRH3) {
        ASSERT_EQ(execYAMLTest(T_PRH3),"OK");
    }

    TEST_F(FromYAMLTest, T_PUW8) {
        ASSERT_EQ(execYAMLTest(T_PUW8),"OK");
    }

    TEST_F(FromYAMLTest, T_PW8X) {
        ASSERT_EQ(execYAMLTest(T_PW8X),"OK");
    }

    TEST_F(FromYAMLTest, T_Q4CL) {
        ASSERT_EQ(execYAMLTest(T_Q4CL),"OK");
    }

    TEST_F(FromYAMLTest, T_Q5MG) {
        ASSERT_EQ(execYAMLTest(T_Q5MG),"OK");
    }

    TEST_F(FromYAMLTest, T_Q88A) {
        ASSERT_EQ(execYAMLTest(T_Q88A),"OK");
    }

    TEST_F(FromYAMLTest, T_Q8AD) {
        ASSERT_EQ(execYAMLTest(T_Q8AD),"OK");
    }

    TEST_F(FromYAMLTest, T_Q9WF) {
        ASSERT_EQ(execYAMLTest(T_Q9WF),"OK");
    }

    TEST_F(FromYAMLTest, T_QB6E) {
        ASSERT_EQ(execYAMLTest(T_QB6E),"OK");
    }

    TEST_F(FromYAMLTest, T_QF4Y) {
        ASSERT_EQ(execYAMLTest(T_QF4Y),"OK");
    }

    TEST_F(FromYAMLTest, T_QLJ7) {
        ASSERT_EQ(execYAMLTest(T_QLJ7),"OK");
    }

    TEST_F(FromYAMLTest, T_QT73) {
        ASSERT_EQ(execYAMLTest(T_QT73),"OK");
    }

    TEST_F(FromYAMLTest, T_R4YG) {
        ASSERT_EQ(execYAMLTest(T_R4YG),"OK");
    }

    TEST_F(FromYAMLTest, T_R52L) {
        ASSERT_EQ(execYAMLTest(T_R52L),"OK");
    }

    TEST_F(FromYAMLTest, T_RHX7) {
        ASSERT_EQ(execYAMLTest(T_RHX7),"OK");
    }

    TEST_F(FromYAMLTest, T_RLU9) {
        ASSERT_EQ(execYAMLTest(T_RLU9),"OK");
    }

    TEST_F(FromYAMLTest, T_RR7F) {
        ASSERT_EQ(execYAMLTest(T_RR7F),"OK");
    }

    TEST_F(FromYAMLTest, T_RTP8) {
        ASSERT_EQ(execYAMLTest(T_RTP8),"OK");
    }

    TEST_F(FromYAMLTest, T_RXY3) {
        ASSERT_EQ(execYAMLTest(T_RXY3),"OK");
    }

    TEST_F(FromYAMLTest, T_RZP5) {
        ASSERT_EQ(execYAMLTest(T_RZP5),"OK");
    }

    TEST_F(FromYAMLTest, T_RZT7) {
        ASSERT_EQ(execYAMLTest(T_RZT7),"OK");
    }

    TEST_F(FromYAMLTest, T_S3PD) {
        ASSERT_EQ(execYAMLTest(T_S3PD),"OK");
    }

    TEST_F(FromYAMLTest, T_S4GJ) {
        ASSERT_EQ(execYAMLTest(T_S4GJ),"OK");
    }

    TEST_F(FromYAMLTest, T_S4JQ) {
        ASSERT_EQ(execYAMLTest(T_S4JQ),"OK");
    }

    TEST_F(FromYAMLTest, T_S4T7) {
        ASSERT_EQ(execYAMLTest(T_S4T7),"OK");
    }

    TEST_F(FromYAMLTest, T_S7BG) {
        ASSERT_EQ(execYAMLTest(T_S7BG),"OK");
    }

    TEST_F(FromYAMLTest, T_S98Z) {
        ASSERT_EQ(execYAMLTest(T_S98Z),"OK");
    }

    TEST_F(FromYAMLTest, T_S9E8) {
        ASSERT_EQ(execYAMLTest(T_S9E8),"OK");
    }

    TEST_F(FromYAMLTest, T_SBG9) {
        ASSERT_EQ(execYAMLTest(T_SBG9),"OK");
    }

    TEST_F(FromYAMLTest, T_SF5V) {
        ASSERT_EQ(execYAMLTest(T_SF5V),"OK");
    }

    TEST_F(FromYAMLTest, T_SKE5) {
        ASSERT_EQ(execYAMLTest(T_SKE5),"OK");
    }

    TEST_F(FromYAMLTest, T_SM9W) {
        ASSERT_EQ(execYAMLTest(T_SM9W),"OK");
    }

    TEST_F(FromYAMLTest, T_SR86) {
        ASSERT_EQ(execYAMLTest(T_SR86),"OK");
    }

    TEST_F(FromYAMLTest, T_SSW6) {
        ASSERT_EQ(execYAMLTest(T_SSW6),"OK");
    }

    TEST_F(FromYAMLTest, T_SU5Z) {
        ASSERT_EQ(execYAMLTest(T_SU5Z),"OK");
    }

    TEST_F(FromYAMLTest, T_SU74) {
        ASSERT_EQ(execYAMLTest(T_SU74),"OK");
    }

    TEST_F(FromYAMLTest, T_SY6V) {
        ASSERT_EQ(execYAMLTest(T_SY6V),"OK");
    }

    TEST_F(FromYAMLTest, T_SYW4) {
        ASSERT_EQ(execYAMLTest(T_SYW4),"OK");
    }

    TEST_F(FromYAMLTest, T_T26H) {
        ASSERT_EQ(execYAMLTest(T_T26H),"OK");
    }

    TEST_F(FromYAMLTest, T_T4YY) {
        ASSERT_EQ(execYAMLTest(T_T4YY),"OK");
    }

    TEST_F(FromYAMLTest, T_T5N4) {
        ASSERT_EQ(execYAMLTest(T_T5N4),"OK");
    }

    TEST_F(FromYAMLTest, T_T833) {
        ASSERT_EQ(execYAMLTest(T_T833),"OK");
    }

    TEST_F(FromYAMLTest, T_TD5N) {
        ASSERT_EQ(execYAMLTest(T_TD5N),"OK");
    }

    TEST_F(FromYAMLTest, T_TE2A) {
        ASSERT_EQ(execYAMLTest(T_TE2A),"OK");
    }

    TEST_F(FromYAMLTest, T_TL85) {
        ASSERT_EQ(execYAMLTest(T_TL85),"OK");
    }

    TEST_F(FromYAMLTest, T_TS54) {
        ASSERT_EQ(execYAMLTest(T_TS54),"OK");
    }

    TEST_F(FromYAMLTest, T_U3C3) {
        ASSERT_EQ(execYAMLTest(T_U3C3),"OK");
    }

    TEST_F(FromYAMLTest, T_U3XV) {
        ASSERT_EQ(execYAMLTest(T_U3XV),"OK");
    }

    TEST_F(FromYAMLTest, T_U44R) {
        ASSERT_EQ(execYAMLTest(T_U44R),"OK");
    }

    TEST_F(FromYAMLTest, T_U99R) {
        ASSERT_EQ(execYAMLTest(T_U99R),"OK");
    }

    TEST_F(FromYAMLTest, T_U9NS) {
        ASSERT_EQ(execYAMLTest(T_U9NS),"OK");
    }

    TEST_F(FromYAMLTest, T_UDM2) {
        ASSERT_EQ(execYAMLTest(T_UDM2),"OK");
    }

    TEST_F(FromYAMLTest, T_UDR7) {
        ASSERT_EQ(execYAMLTest(T_UDR7),"OK");
    }

    TEST_F(FromYAMLTest, T_UGM3) {
        ASSERT_EQ(execYAMLTest(T_UGM3),"OK");
    }

    TEST_F(FromYAMLTest, T_UKK6) {
        ASSERT_EQ(execYAMLTest(T_UKK6),"OK");
    }

    TEST_F(FromYAMLTest, T_UT92) {
        ASSERT_EQ(execYAMLTest(T_UT92),"OK");
    }

    TEST_F(FromYAMLTest, T_UV7Q) {
        ASSERT_EQ(execYAMLTest(T_UV7Q),"OK");
    }

    TEST_F(FromYAMLTest, T_V55R) {
        ASSERT_EQ(execYAMLTest(T_V55R),"OK");
    }

    TEST_F(FromYAMLTest, T_V9D5) {
        ASSERT_EQ(execYAMLTest(T_V9D5),"OK");
    }

    TEST_F(FromYAMLTest, T_VJP3) {
        ASSERT_EQ(execYAMLTest(T_VJP3),"OK");
    }

    TEST_F(FromYAMLTest, T_W42U) {
        ASSERT_EQ(execYAMLTest(T_W42U),"OK");
    }

    TEST_F(FromYAMLTest, T_W4TN) {
        ASSERT_EQ(execYAMLTest(T_W4TN),"OK");
    }

    TEST_F(FromYAMLTest, T_W5VH) {
        ASSERT_EQ(execYAMLTest(T_W5VH),"OK");
    }

    TEST_F(FromYAMLTest, T_W9L4) {
        ASSERT_EQ(execYAMLTest(T_W9L4),"OK");
    }

    TEST_F(FromYAMLTest, T_WZ62) {
        ASSERT_EQ(execYAMLTest(T_WZ62),"OK");
    }

    TEST_F(FromYAMLTest, T_X38W) {
        ASSERT_EQ(execYAMLTest(T_X38W),"OK");
    }

    TEST_F(FromYAMLTest, T_X4QW) {
        ASSERT_EQ(execYAMLTest(T_X4QW),"OK");
    }

    TEST_F(FromYAMLTest, T_X8DW) {
        ASSERT_EQ(execYAMLTest(T_X8DW),"OK");
    }

    TEST_F(FromYAMLTest, T_XLQ9) {
        ASSERT_EQ(execYAMLTest(T_XLQ9),"OK");
    }

    TEST_F(FromYAMLTest, T_XV9V) {
        ASSERT_EQ(execYAMLTest(T_XV9V),"OK");
    }

    TEST_F(FromYAMLTest, T_XW4D) {
        ASSERT_EQ(execYAMLTest(T_XW4D),"OK");
    }

    TEST_F(FromYAMLTest, T_Y2GN) {
        ASSERT_EQ(execYAMLTest(T_Y2GN),"OK");
    }

    TEST_F(FromYAMLTest, T_Y79Y) {
        GTEST_SKIP(); // bug in ryml
        ASSERT_EQ(execYAMLTest(T_Y79Y),"OK");
    }

    TEST_F(FromYAMLTest, T_YD5X) {
        ASSERT_EQ(execYAMLTest(T_YD5X),"OK");
    }

    TEST_F(FromYAMLTest, T_YJV2) {
        ASSERT_EQ(execYAMLTest(T_YJV2),"OK");
    }

    TEST_F(FromYAMLTest, T_Z67P) {
        ASSERT_EQ(execYAMLTest(T_Z67P),"OK");
    }

    TEST_F(FromYAMLTest, T_Z9M4) {
        ASSERT_EQ(execYAMLTest(T_Z9M4),"OK");
    }

    TEST_F(FromYAMLTest, T_ZCZ6) {
        ASSERT_EQ(execYAMLTest(T_ZCZ6),"OK");
    }

    TEST_F(FromYAMLTest, T_ZF4X) {
        ASSERT_EQ(execYAMLTest(T_ZF4X),"OK");
    }

    TEST_F(FromYAMLTest, T_ZH7C) {
        ASSERT_EQ(execYAMLTest(T_ZH7C),"OK");
    }

    TEST_F(FromYAMLTest, T_ZK9H) {
        ASSERT_EQ(execYAMLTest(T_ZK9H),"OK");
    }

    TEST_F(FromYAMLTest, T_ZL4Z) {
        ASSERT_EQ(execYAMLTest(T_ZL4Z),"OK");
    }

    TEST_F(FromYAMLTest, T_ZVH3) {
        ASSERT_EQ(execYAMLTest(T_ZVH3),"OK");
    }

    TEST_F(FromYAMLTest, T_ZWK4) {
        ASSERT_EQ(execYAMLTest(T_ZWK4),"OK");
    }

    TEST_F(FromYAMLTest, T_ZXT5) {
        ASSERT_EQ(execYAMLTest(T_ZXT5),"OK");
    }

    TEST_F(FromYAMLTest, T_ZYU8) {
        ASSERT_EQ(execYAMLTest(T_ZYU8),"OK");
    }

} /* namespace nix */
