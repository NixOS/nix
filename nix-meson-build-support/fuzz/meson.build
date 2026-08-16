# To deduplicate the boilerplate of compiling fuzz harnesses, this file provides
# the shared definitions for that.

fuzzing_engine = get_option('fuzzing-engine')

if fuzzing_engine == ''
  sanitizers_for_project = get_option('b_sanitize').split(',')
  if 'fuzzer-no-link' not in sanitizers_for_project
    error('fuzzers=true requires fuzzer-no-link in b_sanitize')
  endif

  if cxx.get_id() != 'clang'
    # Because we use libFuzzer.
    error('fuzzing requires an LLVM toolchain')
  endif

  sanitizers = []
  foreach i : sanitizers_for_project
    if i != 'fuzzer-no-link'
      sanitizers += i
    endif
  endforeach
  sanitizers += 'fuzzer'

  fuzz_link_args = [
    '-fsanitize=' + ','.join(sanitizers),
    # Linking order shenanigans. For whatever reason libstdc++ gets linked
    # first before libasan when also building with fuzzer sanitizer?
    # LD_PRELOAD is annoying, so it would be nice if this worked out-of-the box.
    # TODO: Make this work with libc++ too.
    '-nostdlib++',
    '-lstdc++',
  ]
else
  fuzz_link_args = [ fuzzing_engine ]
endif

foreach t : fuzz_targets
  executable(
    'fuzz-' + t[0],
    t[1],
    dependencies : fuzz_dependencies,
    link_args : fuzz_link_args,
    install : true,
  )
endforeach
