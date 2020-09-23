PRECOMPILE_HEADERS ?= 0

print-var-help += \
  echo "  PRECOMPILE_HEADERS ($(PRECOMPILE_HEADERS)): Whether to use precompiled headers to speed up the build";

GCH = $(buildprefix)precompiled-headers.h.gch

$(GCH): precompiled-headers.h
	@rm -f $@
	@mkdir -p "$(dir $@)"
	$(trace-gen) $(CXX) -x c++-header -o $@ $< $(GLOBAL_CXXFLAGS) $(GCH_CXXFLAGS)

PCH = $(buildprefix)precompiled-headers.h.pch

$(PCH): precompiled-headers.h
	@rm -f $@
	@mkdir -p "$(dir $@)"
	$(trace-gen) $(CXX) -x c++-header -o $@ $< $(GLOBAL_CXXFLAGS) $(GCH_CXXFLAGS)

clean-files += $(GCH) $(PCH)

ifeq ($(PRECOMPILE_HEADERS), 1)

  ifeq ($(findstring g++,$(CXX)), g++)

    GLOBAL_CXXFLAGS_PCH += -include $(buildprefix)precompiled-headers.h -Winvalid-pch

    GLOBAL_ORDER_AFTER += $(GCH)

  else ifeq ($(findstring clang++,$(CXX)), clang++)

    GLOBAL_CXXFLAGS_PCH += -include-pch $(PCH) -Winvalid-pch

    GLOBAL_ORDER_AFTER += $(PCH)

  else

    $(error Don't know how to precompile headers on $(CXX))

  endif

endif
