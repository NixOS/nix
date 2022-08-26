{ command }:

with builtins;
with import ./utils.nix;

let

  showCommand = { command, def, filename }:
    let
      showSynopsis = command: args:
        let
          showArgument = arg: "*${arg.label}*" + (if arg ? arity then "" else "...");
          arguments = concatStringsSep " " (map showArgument args);
        in ''
         `${command}` [*option*...] ${arguments}
        '';
      maybeSubcommands = if def ? commands && def.commands != {}
        then ''
           where *subcommand* is one of the following:

           ${subcommands}
         ''
        else "";
      subcommands = if length categories > 1
        then listCategories
        else listSubcommands def.commands;
      categories = sort (x: y: x.id < y.id) (unique (map (cmd: cmd.category) (attrValues def.commands)));
      listCategories = concatStrings (map showCategory categories);
      showCategory = cat: ''
        **${toString cat.description}:**

        ${listSubcommands (filterAttrs (n: v: v.category == cat) def.commands)}
      '';
      listSubcommands = cmds: concatStrings (attrValues (mapAttrs showSubcommand cmds));
      showSubcommand = name: subcmd: ''
        * [`${command} ${name}`](./${appendName filename name}.md) - ${subcmd.description}
      '';
      maybeDocumentation = if def ? doc then def.doc else "";
      maybeOptions = if def.flags == {} then "" else ''
        # Options

        ${showOptions def.flags}
      '';
      showOptions = options:
        let
          showCategory = cat: ''
            ${if cat != "" then "**${cat}:**" else ""}

            ${listOptions (filterAttrs (n: v: v.category == cat) options)}
            '';
          listOptions = opts: concatStringsSep "\n" (attrValues (mapAttrs showOption opts));
          showOption = name: option:
            let
              shortName = if option ? shortName then "/ `-${option.shortName}`" else "";
              labels = if option ? labels then (concatStringsSep " " (map (s: "*${s}*") option.labels)) else "";
            in trim ''
              - `--${name}` ${shortName} ${labels}

                ${option.description}
            '';
          categories = sort builtins.lessThan (unique (map (cmd: cmd.category) (attrValues options)));
        in concatStrings (map showCategory categories);
    in squash  ''
      > **Warning** \
      > This program is **experimental** and its interface is subject to change.

      # Name

      `${command}` - ${def.description}

      # Synopsis

      ${showSynopsis command def.args}

      ${maybeSubcommands}

      ${maybeDocumentation}

      ${maybeOptions}
    '';

  appendName = filename: name: (if filename == "nix" then "nix3" else filename) + "-" + name;

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
