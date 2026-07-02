# This is just a top-level project to include all the others as subprojects
# for development shell purposes (when building via Nix) or for distro packaging purposes.

project(
  'Nix',
  'cpp',
  version : files('.version'),
  subproject_dir : 'src',
  meson_version : '>= 1.8',
)

# Internal Libraries
subproject('libutil')
subproject('libstore')
subproject('libfetchers')
subproject('libexpr')
subproject('libflake')
subproject('libmain')
subproject('libcmd')

# External C wrapper libraries
subproject('libutil-c')
subproject('libstore-c')
subproject('libfetchers-c')
subproject('libexpr-c')
subproject('libflake-c')
subproject('libmain-c')

# Executables
subproject('nix')

if host_machine.system() == 'linux'
  subproject('nswrapper')
endif

# Docs
if get_option('doc-gen')
  subproject('internal-api-docs')
  subproject('external-api-docs')
  if meson.can_run_host_binaries()
    subproject('nix-manual')
  endif
endif

# Testing
if get_option('unit-tests')
  subproject('libutil-test-support')
  subproject('libutil-tests')
  subproject('libstore-test-support')
  subproject('libstore-tests')
  subproject('libfetchers-tests')
  subproject('libexpr-test-support')
  subproject('libexpr-tests')
  subproject('libflake-tests')
  subproject('libcmd-tests')
endif

if get_option('functional-tests')
  subproject('nix-functional-tests')
endif

if get_option('json-schema-checks')
  subproject('json-schema-checks')
endif

# Static Analysis
# Build the custom clang-tidy plugin if LLVM is available.
# required: false → gracefully skipped if dependency('LLVM') fails.
clang_tidy_plugin_sp = subproject('clang-tidy-plugin', required : false)
subdir('nix-meson-build-support/common/clang-tidy')
