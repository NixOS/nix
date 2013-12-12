%.o: %.cc
	$(QUIET) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(basename $@).dep -MP

%.o: %.cpp
	$(QUIET) $(CXX) -o $@ -c $< $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($@_CXXFLAGS) -MMD -MF $(basename $@).dep -MP

%.o: %.c
	$(QUIET) $(CC) -o $@ -c $< $(GLOBAL_CFLAGS) $(CFLAGS) $($@_CFLAGS) -MMD -MF $(basename $@).dep -MP
