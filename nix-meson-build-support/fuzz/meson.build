# To deduplicate the boilerplate of compiling fuzz harnesses, this file provides
# the shared definitions for that.

if cxx.get_id() != 'clang'
  # Because we use libFuzzer.
  error('fuzzing is requires an LLVM toolchain')
endif

foreach t : fuzz_targets
  sanitizers_for_project = get_option('b_sanitize').split(',')
  sanitizers = []
  foreach i : sanitizers_for_project
    if i != 'fuzzer-no-link'
      sanitizers += i
    endif
  endforeach
  sanitizers += 'fuzzer'

  executable(
    'fuzz-' + t[0],
    t[1],
    dependencies : deps_private_subproject + deps_private + deps_other,
    link_args : [
      '-fsanitize=' + ','.join(sanitizers),
      # Linking order shenanigans. For whatever reason libstdc++ gets linked
      # first before libasan when also building with fuzzer sanitizer?
      # LD_PRELOAD is annoying, so it would be nice if this worked out-of-the box.
      # TODO: Make this work with libc++ too.
      '-nostdlib++',
      '-lstdc++',
    ],
    install : true,
  )
endforeach
