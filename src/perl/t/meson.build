# Nix-Perl Tests
#============================================================================


# src
#---------------------------------------------------

nix_perl_tests = files(
  'init.t',
  # hack for trailing newline
)


foreach f : nix_perl_tests
  fs.copyfile(f)
endforeach
