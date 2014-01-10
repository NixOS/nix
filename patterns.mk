%.o: %.cc
	$(trace-cxx) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(basename $@).dep -MP

%.o: %.cpp
	$(trace-cxx) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(basename $@).dep -MP

%.o: %.c
	$(trace-cc) $(CC) -o $@ -c $< $(GLOBAL_CFLAGS) $(CFLAGS) $($@_CFLAGS) -MMD -MF $(basename $@).dep -MP
