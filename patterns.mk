%.o: %.cc
	$(trace-cxx) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(call filename-to-dep, $@) -MP

%.o: %.cpp
	$(trace-cxx) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(call filename-to-dep, $@) -MP

%.o: %.c
	$(trace-cc) $(CC) -o $@ -c $< $(GLOBAL_CFLAGS) $(CFLAGS) $($@_CFLAGS) -MMD -MF $(call filename-to-dep, $@) -MP
