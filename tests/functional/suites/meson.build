nix_expr = dependency('nix-expr', required : false)

if nix_expr.found() and get_option('default_library') != 'static'
  subdir('plugins')
endif

subdir('repl')
