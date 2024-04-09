let
  inherit (builtins)
    attrNames
    attrValues
    concatMap
    concatStringsSep
    fromJSON
    groupBy
    length
    lessThan
    listToAttrs
    mapAttrs
    match
    replaceStrings
    sort
    ;
  inherit (import <nix/utils.nix>)
    attrsToList
    concatStrings
    filterAttrs
    optionalString
    squash
    trim
    unique
    ;
  showStoreDocs = import <nix/generate-store-info.nix>;
in

inlineHTML: commandDump:

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

        ${maybeProse}

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

      maybeProse =
        # FIXME: this is a horrible hack to keep `nix help-stores` working.
        let
          help-stores = ''
            ${index}

            ${allStores}
          '';
          index = replaceStrings
            [ "@store-types@" "./local-store.md" "./local-daemon-store.md" ]
            [ storesOverview "#local-store" "#local-daemon-store" ]
            details.doc;
          storesOverview =
            let
              showEntry = store:
                "- [${store.name}](#${store.slug})";
            in
            concatStringsSep "\n" (map showEntry storesList) + "\n";
          allStores = concatStringsSep "\n" (attrValues storePages);
          storePages = listToAttrs
            (map (s: { name = s.filename; value = s.page; }) storesList);
          storesList = showStoreDocs {
            storeInfo = commandInfo.stores;
            inherit inlineHTML;
          };
        in
        optionalString (details ? doc) (
          if match ".*@store-types@.*" details.doc != null
          then help-stores
          else details.doc
        );

      maybeOptions =
        let
          allVisibleOptions = filterAttrs
            (_: o: ! o.hiddenCategory)
            (details.flags // toplevel.flags);
        in
        optionalString (allVisibleOptions != { }) ''
          # Options

          ${showOptions inlineHTML allVisibleOptions}

          > **Note**
          >
          > See [`man nix.conf`](@docroot@/command-ref/conf-file.md#command-line-flags) for overriding configuration settings with command line flags.
        '';

      showOptions = inlineHTML: allOptions:
        let
          showCategory = cat: opts: ''
            ${optionalString (cat != "") "## ${cat}"}

            ${concatStringsSep "\n" (attrValues (mapAttrs showOption opts))}
          '';
          showOption = name: option:
            let
              result = trim ''
                - ${item}

                  ${option.description}
              '';
              item = if inlineHTML
                then ''<span id="opt-${name}">[`--${name}`](#opt-${name})</span> ${shortName} ${labels}''
                else "`--${name}` ${shortName} ${labels}";
              shortName = optionalString
                (option ? shortName)
                ("/ `-${option.shortName}`");
              labels = optionalString
                (option ? labels)
                (concatStringsSep " " (map (s: "*${s}*") option.labels));
            in result;
          categories = mapAttrs
            # Convert each group from a list of key-value pairs back to an attrset
            (_: listToAttrs)
            (groupBy
              (cmd: cmd.value.category)
              (attrsToList allOptions));
        in concatStrings (attrValues (mapAttrs showCategory categories));
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

in (listToAttrs manpages) // { "SUMMARY.md" = tableOfContents; }
