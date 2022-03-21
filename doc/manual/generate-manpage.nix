{ command, renderLinks ? false }:

with builtins;
with import ./utils.nix;

let

  showCommand =
    { command, def, filename }:
    ''
      **Warning**: This program is **experimental** and its interface is subject to change.
    ''
    + "# Name\n\n"
    + "`${command}` - ${def.description}\n\n"
    + "# Synopsis\n\n"
    + showSynopsis { inherit command; args = def.args; }
    + (if def.commands or {} != {}
       then
         let
           categories = sort (x: y: x.id < y.id) (unique (map (cmd: cmd.category) (attrValues def.commands)));
           listCommands = cmds:
             concatStrings (map (name:
               "* "
               + (if renderLinks
                  then "[`${command} ${name}`](./${appendName filename name}.md)"
                  else "`${command} ${name}`")
               + " - ${cmds.${name}.description}\n")
               (attrNames cmds));
         in
         "where *subcommand* is one of the following:\n\n"
         # FIXME: group by category
         + (if length categories > 1
            then
              concatStrings (map
                (cat:
                  "**${toString cat.description}:**\n\n"
                  + listCommands (filterAttrs (n: v: v.category == cat) def.commands)
                  + "\n"
                ) categories)
              + "\n"
            else
              listCommands def.commands
              + "\n")
       else "")
    + (if def ? doc
       then def.doc + "\n\n"
       else "")
    + (let s = showOptions def.flags; in
       if s != ""
       then "# Options\n\n${s}"
       else "")
  ;

  appendName = filename: name: (if filename == "nix" then "nix3" else filename) + "-" + name;

  showOptions = flags:
    let
      categories = sort builtins.lessThan (unique (map (cmd: cmd.category) (attrValues flags)));
    in
      concatStrings (map
        (cat:
          (if cat != ""
           then "**${cat}:**\n\n"
           else "")
          + concatStrings
            (map (longName:
              let
                flag = flags.${longName};
              in
                "  - `--${longName}`"
                + (if flag ? shortName then " / `-${flag.shortName}`" else "")
                + (if flag ? labels then " " + (concatStringsSep " " (map (s: "*${s}*") flag.labels)) else "")
                + "  \n"
                + "    " + flag.description + "\n\n"
            ) (attrNames (filterAttrs (n: v: v.category == cat) flags))))
        categories);

  showSynopsis =
    { command, args }:
    "`${command}` [*option*...] ${concatStringsSep " "
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
  manpages = processCommand { filename = "nix"; command = "nix"; def = builtins.fromJSON command; };
  summary = concatStrings (map (manpage: "    - [${manpage.command}](command-ref/new-cli/${manpage.name})\n") manpages);
in
(listToAttrs manpages) // { "SUMMARY.md" = summary; }
