{ command }:

with builtins;
with import ./utils.nix;

let

  showCommand = { command, details, filename }:
    let
      result = ''
        > **Warning** \
        > This program is **experimental** and its interface is subject to change.

        # Name

        `${command}` - ${details.description}

        # Synopsis

        ${showSynopsis command details.args}

        ${maybeSubcommands}

        ${maybeDocumentation}

        ${maybeOptions}
      '';
      showSynopsis = command: args:
        let
          showArgument = arg: "*${arg.label}*" + (if arg ? arity then "" else "...");
          arguments = concatStringsSep " " (map showArgument args);
        in ''
         `${command}` [*option*...] ${arguments}
        '';
      maybeSubcommands = if details ? commands && details.commands != {}
        then ''
           where *subcommand* is one of the following:

           ${subcommands}
         ''
        else "";
      subcommands = if length categories > 1
        then listCategories
        else listSubcommands details.commands;
      categories = sort (x: y: x.id < y.id) (unique (map (cmd: cmd.category) (attrValues details.commands)));
      listCategories = concatStrings (map showCategory categories);
      showCategory = cat: ''
        **${toString cat.description}:**

        ${listSubcommands (filterAttrs (n: v: v.category == cat) details.commands)}
      '';
      listSubcommands = cmds: concatStrings (attrValues (mapAttrs showSubcommand cmds));
      showSubcommand = name: subcmd: ''
        * [`${command} ${name}`](./${appendName filename name}.md) - ${subcmd.description}
      '';
      maybeDocumentation = if details ? doc then details.doc else "";
      maybeOptions = if details.flags == {} then "" else ''
        # Options

        ${showOptions details.flags}
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
    in squash result;

  appendName = filename: name: (if filename == "nix" then "nix3" else filename) + "-" + name;

  processCommand = { command, details, filename }:
    let
      cmd = {
        inherit command;
        name = filename + ".md";
        value = showCommand { inherit command details filename; };
      };
      subcommand = subCmd: processCommand {
        command = command + " " + subCmd;
        details = details.commands.${subCmd};
        filename = appendName filename subCmd;
      };
    in [ cmd ] ++ concatMap subcommand (attrNames details.commands or {});

  manpages = processCommand {
    command = "nix";
    details = builtins.fromJSON command;
    filename = "nix";
  };

  tableOfContents = let
    showEntry = page:
      "    - [${page.command}](command-ref/new-cli/${page.name})";
    in concatStringsSep "\n" (map showEntry manpages);

in (listToAttrs manpages) // { "SUMMARY.md" = tableOfContents; }
