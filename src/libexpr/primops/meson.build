foreach header : [
  'derivation.nix',
]
  generated_headers += custom_target(
    command : [ 'bash', '-c', '{ echo \'R"__NIX_STR(\' && cat @INPUT@ && echo \')__NIX_STR"\'; } > "$1"', '_ignored_argv0', '@OUTPUT@' ],
    input : header,
    output : '@PLAINNAME@.gen.hh',
  )
endforeach

sources += files(
  'context.cc',
  'fetchClosure.cc',
  'fetchMercurial.cc',
  'fetchTree.cc',
  'fromTOML.cc',
)
