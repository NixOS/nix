# This is just a stub project to include all the others as subprojects
# for development shell purposes

project('nix-dev-shell', 'cpp',
  version : files('.version'),
  subproject_dir : 'src',
)

subproject('libutil')
subproject('libstore')
subproject('libfetchers')
subproject('perl')
subproject('internal-api-docs')
subproject('external-api-docs')

# C wrappers
subproject('libutil-c')

# Testing
subproject('libutil-test-support')
subproject('libutil-test')
