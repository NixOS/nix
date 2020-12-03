command:

with builtins;
with import ./utils.nix;

let

  showCommand =
    { command, def, filename }:
    "# Name\n\n"
    + "`${command}` - ${def.description}\n\n"
    + "# Synopsis\n\n"
    + showSynopsis { inherit command; args = def.args; }
    + (if def.commands or {} != {}
       then
         "where *subcommand* is one of the following:\n\n"
         # FIXME: group by category
         + concatStrings (map (name:
           "* [`${command} ${name}`](./${appendName filename name}.md) - ${def.commands.${name}.description}\n")
           (attrNames def.commands))
         + "\n"
       else "")
    + (if def.examples or [] != []
       then
         "# Examples\n\n"
         + concatStrings (map ({ description, command }: "${description}\n\n```console\n${command}\n```\n\n") def.examples)
       else "")
    + (if def ? doc
       then def.doc + "\n\n"
       else "")
    + (let s = showFlags def.flags; in
       if s != ""
       then "# Flags\n\n${s}"
       else "")
  ;

  appendName = filename: name: (if filename == "nix" then "nix3" else filename) + "-" + name;

  showFlags = flags:
    concatStrings
      (map (longName:
        let flag = flags.${longName}; in
        if flag.category or "" != "config"
        then
          "  - `--${longName}`"
          + (if flag ? shortName then " / `${flag.shortName}`" else "")
          + (if flag ? labels then " " + (concatStringsSep " " (map (s: "*${s}*") flag.labels)) else "")
          + "  \n"
          + "    " + flag.description + "\n\n"
        else "")
        (attrNames flags));

  showSynopsis =
    { command, args }:
    "`${command}` [*flags*...] ${concatStringsSep " "
      (map (arg: "*${arg.label}*" + (if arg ? arity then "" else "...")) args)}\n\n";

  processCommand = { command, def, filename }:
    [ { name = filename + ".md"; value = showCommand { inherit command def filename; }; inherit command; } ]
    ++ concatMap
      (name: processCommand {
        filename = appendName filename name;
        command = command + " " + name;
        def = def.commands.${name};
      })
      (attrNames def.commands or {});

in

let
  manpages = processCommand { filename = "nix"; command = "nix"; def = command; };
  summary = concatStrings (map (manpage: "    - [${manpage.command}](command-ref/new-cli/${manpage.name})\n") manpages);
in
(listToAttrs manpages) // { "SUMMARY.md" = summary; }
