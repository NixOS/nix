compile-commands-json-files :=

define write-compile-commands
  _srcs := $$(sort $$(foreach src, $$($(1)_SOURCES), $$(src)))

  $(1)_COMPILE_COMMANDS_JSON := $$(addprefix $(buildprefix), $$(addsuffix .compile_commands.json, $$(basename $$(_srcs))))

  compile-commands-json-files += $$($(1)_COMPILE_COMMANDS_JSON)

  clean-files += $$($(1)_COMPILE_COMMANDS_JSON)
endef
