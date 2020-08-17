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

"`" + .command + "` " + (.args | map("*" + .label + "*" + (if has("arity") then "" else "..." end)) | join(" ")) + "\n"

;

"# Synopsis\n\n"
+ ({"command": "nix", "args": .args} | show_synopsis)
+ "\n"
+ "# Common flags\n\n"
+ show_flags
+ (.commands | to_entries | map(
    "# Operation `" + .key + "`\n\n"
    + "## Synopsis\n\n"
    + ({"command": ("nix " + .key), "args": .value.args} | show_synopsis)
    + "\n"
    + "## Description\n\n"
    + .value.description + "\n\n"
    + (if (.value.flags | length) > 0 then
        "## Flags\n\n"
        + (.value | show_flags)
      else "" end)
    + (if (.value.examples | length) > 0 then
        "## Examples\n\n"
        + (.value.examples | map("- " + .description + "\n\n  ```console\n  " + .command + "\n  ```\n" ) | join("\n"))
        + "\n"
       else "" end)
  ) | join(""))
