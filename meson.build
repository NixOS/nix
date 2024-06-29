# This is just a stub project to include all the others as subprojects
# for development shell purposes

project('nix-dev-shell', 'cpp',
  version : files('.version'),
  subproject_dir : 'src',
)

subproject('libutil')
subproject('libstore')
subproject('libfetchers')
subproject('libexpr')
subproject('libflake')

# Docs
subproject('internal-api-docs')
subproject('external-api-docs')

# C wrappers
subproject('libutil-c')
subproject('libstore-c')
subproject('libexpr-c')

# Language Bindings
subproject('perl')

# Testing
subproject('nix-util-test-support')
subproject('nix-util-tests')
subproject('nix-store-test-support')
subproject('nix-store-tests')
subproject('nix-fetchers-tests')
subproject('nix-expr-test-support')
subproject('nix-expr-tests')
subproject('nix-flake-tests')
