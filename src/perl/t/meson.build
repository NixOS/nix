# Nix-Perl Tests
#============================================================================


# src
#---------------------------------------------------

nix_perl_tests = files(
  'init.t',
)

nix_perl_tests_copy_tgts = []
foreach f : nix_perl_tests
  nix_perl_tests_copy_tgts += fs.copyfile(f)
endforeach
