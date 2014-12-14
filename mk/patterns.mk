$(buildprefix)%.o: %.cc
	@mkdir -p "$(dir $@)"
	$(trace-cxx) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(GLOBAL_CXXFLAGS_PCH) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(call filename-to-dep, $@) -MP

$(buildprefix)%.o: %.cpp
	@mkdir -p "$(dir $@)"
	$(trace-cxx) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(GLOBAL_CXXFLAGS_PCH) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(call filename-to-dep, $@) -MP

$(buildprefix)%.o: %.c
	@mkdir -p "$(dir $@)"
	$(trace-cc) $(CC) -o $@ -c $< $(GLOBAL_CFLAGS) $(CFLAGS) $($@_CFLAGS) -MMD -MF $(call filename-to-dep, $@) -MP
