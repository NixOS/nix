flex = find_program('flex')
bison = find_program('bison')

parser_tab_cchh = custom_target(
  'parser_tab.[cchh]',
  output : ['parser_tab.cc', 'parser_tab.hh'],
  input : 'parser.y',
  command : [bison, '-v', '-o', '@OUTPUT0@', '@INPUT@', '-d']
)

lexer_tab_cchh = custom_target(
  'lexer_tab.[cchh]',
  output : ['lexer_tab.cc', 'lexer_tab.hh'],
  input : 'lexer.l',
  command : [
    flex,
    '--outfile', '@OUTPUT0@',
    '--headerfile', '@OUTPUT1@',
    '@INPUT@'
  ]
)

libexpr = library(
  'libnixexpr',
  [
    'attr-path.cc',
    'attr-set.cc',
    'common-eval-args.cc',
    'eval.cc',
    'get-drvs.cc',
    'json-to-value.cc',
    'names.cc',
    'nixexpr.cc',
    'primops.cc',
    'value-to-json.cc',
    'value-to-xml.cc',

    lexer_tab_cchh[0],
    parser_tab_cchh[0],
  ],
  install : true,
  include_directories : global_subdirs,
  dependencies : [
    global_libs,
  ],
  link_with : [
    libstore,
    libutil,
  ],
  link_args : host_machine.system() == 'freebsd' ? ['-ldl'] : []
)

libraries += [ libexpr ]
