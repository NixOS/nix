V ?= 0

ifeq ($(V), 0)

  trace-gen     = @echo "  GEN   " $@;
  trace-cc      = @echo "  CC    " $@;
  trace-cxx     = @echo "  CXX   " $@;
  trace-ld      = @echo "  LD    " $@;
  trace-ar      = @echo "  AR    " $@;
  trace-install = @echo "  INST  " $@;
  trace-javac   = @echo "  JAVAC " $@;
  trace-jar     = @echo "  JAR   " $@;
  trace-mkdir   = @echo "  MKDIR " $@;

  suppress  = @

endif
