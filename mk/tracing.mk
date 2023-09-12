V ?= 0

ifeq ($(V), 0)

  trace-gen     = @echo "  GEN   " $@;
  trace-cc      = @echo "  CC    " $@;
  trace-cxx     = @echo "  CXX   " $@;
  trace-ld      = @echo "  LD    " $@;
  trace-ar      = @echo "  AR    " $@;
  trace-install = @echo "  INST  " $@;
  trace-mkdir   = @echo "  MKDIR " $@;
  trace-test    = @echo "  TEST  " $@;

  suppress  = @

endif
