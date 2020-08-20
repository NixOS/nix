def show_flags:
  .flags
  | map_values(select(.category != "config"))
  | to_entries
  | map(
      "  - `--" + .key + "`"
      + (if .value.shortName then " / `" + .value.shortName + "`" else "" end)
      + (if .value.labels then " " + (.value.labels | map("*" + . + "*") | join(" ")) else "" end)
      + "  \n"
      + "    " + .value.description + "\n\n")
  | join("")
  ;

def show_synopsis:
  "`" + .command + "` [*flags*...] " + (.args | map("*" + .label + "*" + (if has("arity") then "" else "..." end)) | join(" ")) + "\n\n"
  ;

def show_command:
  . as $top |
  .section + " Name\n\n"
  + "`" + .command + "` - " + .def.description + "\n\n"
  + .section + " Synopsis\n\n"
  + ({"command": .command, "args": .def.args} | show_synopsis)
  + (if .def | has("doc")
     then .section + " Description\n\n" + .def.doc + "\n\n"
     else ""
     end)
  + (if (.def.flags | length) > 0 then
      .section + " Flags\n\n"
      + (.def | show_flags)
    else "" end)
  + (if (.def.examples | length) > 0 then
      .section + " Examples\n\n"
      + (.def.examples | map(.description + "\n\n```console\n" + .command + "\n```\n" ) | join("\n"))
      + "\n"
     else "" end)
  + (if .def.commands then .def.commands | to_entries | map(
      "# Subcommand `" + ($top.command + " " + .key) + "`\n\n"
      + ({"command": ($top.command + " " + .key), "section": "##", "def": .value} | show_command)
    ) | join("") else "" end)
  ;

"Title: nix\n\n"
+ ({"command": "nix", "section": "#", "def": .} | show_command)
