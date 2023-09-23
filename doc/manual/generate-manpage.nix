let
  inherit (builtins)
    attrNames attrValues fromJSON listToAttrs mapAttrs
    concatStringsSep concatMap length lessThan replaceStrings sort;
  inherit (import ./utils.nix) concatStrings optionalString filterAttrs trim squash unique showSettings;
in

commandDump:

let

  commandInfo = fromJSON commandDump;

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

      # TODO: move this confusing special case out of here when implementing #8496
      maybeDocumentation = optionalString
        (details ? doc)
        (replaceStrings ["@stores@"] [storeDocs] details.doc);

      maybeOptions = optionalString (details.flags != {}) ''
        # Options

        ${showOptions (details.flags // toplevel.flags)}

        > **Note**
        >
        > See [`man nix.conf`](@docroot@/command-ref/conf-file.md#command-line-flags) for overriding configuration settings with command line flags.
      '';

      showOptions = allOptions:
        let
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
              - <span id="opt-${name}">[`--${name}`](#opt-${name})</span> ${shortName} ${labels}

                ${option.description}
            '';
          categories = sort lessThan (unique (map (cmd: cmd.category) (attrValues allOptions)));
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

  manpages = processCommand {
    command = "nix";
    details = commandInfo.args;
    filename = "nix";
    toplevel = commandInfo.args;
  };

  tableOfContents = let
    showEntry = page:
      "    - [${page.command}](command-ref/new-cli/${page.name})";
    in concatStringsSep "\n" (map showEntry manpages) + "\n";

  storeDocs =
    let
      showStore = name: { settings, doc, experimentalFeature }:
        let
          experimentalFeatureNote = optionalString (experimentalFeature != null) ''
            > **Warning**
            > This store is part of an
            > [experimental feature](@docroot@/contributing/experimental-features.md).

            To use this store, you need to make sure the corresponding experimental feature,
            [`${experimentalFeature}`](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature}),
            is enabled.
            For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):

            ```
            extra-experimental-features = ${experimentalFeature}
            ```
          '';
        in ''
          ## ${name}

          ${doc}

          ${experimentalFeatureNote}

          **Settings**:

          ${showSettings { useAnchors = false; } settings}
        '';
    in concatStrings (attrValues (mapAttrs showStore commandInfo.stores));

in (listToAttrs manpages) // { "SUMMARY.md" = tableOfContents; }
