# This is just a stub project to include all the others as subprojects
# for development shell purposes

project('nix-dev-shell', 'cpp',
  version : files('.version'),
  default_options : [
    'localstatedir=/nix/var',
  ]
)

# Internal Libraries
subproject('libutil')
subproject('libstore')
subproject('libfetchers')
subproject('libexpr')
subproject('libflake')
subproject('libmain')
subproject('libcmd')

# Executables
subproject('nix')

# Docs
subproject('internal-api-docs')
subproject('external-api-docs')

# External C wrapper libraries
subproject('libutil-c')
subproject('libstore-c')
subproject('libexpr-c')
subproject('libmain-c')

# Language Bindings
if not meson.is_cross_build()
  subproject('perl')
endif

# Testing
subproject('nix-util-test-support')
subproject('nix-util-tests')
subproject('nix-store-test-support')
subproject('nix-store-tests')
subproject('nix-fetchers-tests')
subproject('nix-expr-test-support')
subproject('nix-expr-tests')
subproject('nix-flake-tests')
subproject('nix-functional-tests')
