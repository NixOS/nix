
# These are the complete command lines we use to compile C and C++ files.
# - $< is the source file.
# - $1 is the object file to create.
CC_CMD=$(CC) -o $1 -c $< $(CPPFLAGS) $(GLOBAL_CFLAGS) $(CFLAGS) $($1_CFLAGS) -MMD -MF $(call filename-to-dep,$1) -MP
CXX_CMD=$(CXX) -o $1 -c $< $(CPPFLAGS) $(GLOBAL_CXXFLAGS_PCH) $(GLOBAL_CXXFLAGS) $(CXXFLAGS) $($1_CXXFLAGS) $(ERROR_SWITCH_ENUM) -MMD -MF $(call filename-to-dep,$1) -MP

# We use COMPILE_COMMANDS_JSON_CMD to turn a compilation command (like CC_CMD
# or CXX_CMD above) into a comple_commands.json file. We rely on bash native
# word splitting to define the positional arguments.
# - $< is the source file being compiled.
COMPILE_COMMANDS_JSON_CMD=jq --null-input '{ directory: $$ENV.PWD, file: "$<", arguments: $$ARGS.positional }' --args --


$(buildprefix)%.o: %.cc
	@mkdir -p "$(dir $@)"
	$(trace-cxx) $(call CXX_CMD,$@)

$(buildprefix)%.o: %.cpp
	@mkdir -p "$(dir $@)"
	$(trace-cxx) $(call CXX_CMD,$@)

$(buildprefix)%.o: %.c
	@mkdir -p "$(dir $@)"
	$(trace-cc) $(call CC_CMD,$@)

# In the following we need to replace the .compile_commands.json extension in $@ with .o
# to make the object file. This is needed because CC_CMD and CXX_CMD do further expansions
# based on the object file name (i.e. *_CXXFLAGS and filename-to-dep).

$(buildprefix)%.compile_commands.json: %.cc
	@mkdir -p "$(dir $@)"
	$(trace-jq) $(COMPILE_COMMANDS_JSON_CMD) $(call CXX_CMD,$(@:.compile_commands.json=.o)) > $@

$(buildprefix)%.compile_commands.json: %.cpp
	@mkdir -p "$(dir $@)"
	$(trace-jq) $(COMPILE_COMMANDS_JSON_CMD) $(call CXX_CMD,$(@:.compile_commands.json=.o)) > $@

$(buildprefix)%.compile_commands.json: %.c
	@mkdir -p "$(dir $@)"
	$(trace-jq) $(COMPILE_COMMANDS_JSON_CMD) $(call CC_CMD,$(@:.compile_commands.json=.o)) > $@
