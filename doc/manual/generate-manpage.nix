cliDumpStr:

with builtins;
with import <nix/utils.nix>;

let

  showCommand = { command, details, filename, toplevel }:
    let

      result = ''
        > **Warning** \
        > This program is
        > [**experimental**](@docroot@/contributing/experimental-features.md#xp-feature-nix-command)
        > and its interface is subject to change.

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
          showArgument = arg: "*${arg.label}*" + optionalString (! arg ? arity) "...";
          arguments = concatStringsSep " " (map showArgument args);
        in ''
         `${command}` [*option*...] ${arguments}
        '';

      maybeSubcommands = optionalString (details ? commands && details.commands != {})
         ''
           where *subcommand* is one of the following:

           ${subcommands}
         '';

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

      maybeDocumentation = optionalString
        (details ? doc)
        (replaceStrings ["@stores@"] [storeDocs] details.doc);

      maybeOptions = optionalString (details.flags != {}) ''
        # Options

        ${showOptions details.flags toplevel.flags}
      '';

      showOptions = options: commonOptions:
        let
          allOptions = options // commonOptions;
          showCategory = cat: ''
            ${optionalString (cat != "") "**${cat}:**"}

            ${listOptions (filterAttrs (n: v: v.category == cat) allOptions)}
            '';
          listOptions = opts: concatStringsSep "\n" (attrValues (mapAttrs showOption opts));
          showOption = name: option:
            let
              shortName = optionalString
                (option ? shortName)
                ("/ `-${option.shortName}`");
              labels = optionalString
                (option ? labels)
                (concatStringsSep " " (map (s: "*${s}*") option.labels));
            in trim ''
              - `--${name}` ${shortName} ${labels}

                ${option.description}
            '';
          categories = sort builtins.lessThan (unique (map (cmd: cmd.category) (attrValues allOptions)));
        in concatStrings (map showCategory categories);
    in squash result;

  appendName = filename: name: (if filename == "nix" then "nix3" else filename) + "-" + name;

  processCommand = { command, details, filename, toplevel }:
    let
      cmd = {
        inherit command;
        name = filename + ".md";
        value = showCommand { inherit command details filename toplevel; };
      };
      subcommand = subCmd: processCommand {
        command = command + " " + subCmd;
        details = details.commands.${subCmd};
        filename = appendName filename subCmd;
        inherit toplevel;
      };
    in [ cmd ] ++ concatMap subcommand (attrNames details.commands or {});

  cliDump = builtins.fromJSON cliDumpStr;

  manpages = processCommand {
    command = "nix";
    details = cliDump.args;
    filename = "nix";
    toplevel = cliDump.args;
  };

  tableOfContents = let
    showEntry = page:
      "    - [${page.command}](command-ref/new-cli/${page.name})";
    in concatStringsSep "\n" (map showEntry manpages) + "\n";

  storeDocs =
    let
      showStore = name: { settings, doc }:
        ''
          ## ${name}

          ${doc}

          **Settings**:

          ${showSettings { useAnchors = false; } settings}
        '';
    in concatStrings (attrValues (mapAttrs showStore cliDump.stores));

in (listToAttrs manpages) // { "SUMMARY.md" = tableOfContents; }
