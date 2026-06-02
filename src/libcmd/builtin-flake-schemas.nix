{
  description = "Schemas for well-known Nix flake output types";

  outputs =
    { self }:
    let
      mapAttrsToList = f: attrs: map (name: f name attrs.${name}) (builtins.attrNames attrs);

      checkModule =
        module_:
        let
          module = if builtins.isPath module_ then import module_ else module_;
        in
        builtins.isAttrs module || builtins.isFunction module;

      mkApp = system: app: {
        forSystems = [ system ];
        evalChecks.isValidApp =
          app ? type
          && app.type == "app"
          && app ? program
          && builtins.isString app.program
          &&
            builtins.removeAttrs app [
              "type"
              "program"
              "meta"
            ] == { };
        what = "app";
        shortDescription = app.meta.description or "";
      };

      mkPackage = isFlakeCheck: what: system: package: {
        forSystems = [ system ];
        shortDescription = package.meta.description or "";
        derivationAttrPath = [ ];
        inherit what isFlakeCheck;
      };

      singleDerivationInventory =
        what: isFlakeCheck: output:
        self.lib.mkChildren (builtins.mapAttrs (mkPackage isFlakeCheck what) output);

      schemasSchema = doc: {
        version = 1;
        inherit doc;
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (schemaName: schemaDef: {
              shortDescription = "A schema checker for the `${schemaName}` flake output";
              evalChecks.isValidSchema =
                schemaDef.version or 0 == 1
                && schemaDef ? doc
                && builtins.isString (schemaDef.doc)
                && schemaDef ? inventory
                && builtins.isFunction (schemaDef.inventory);
              what = "flake schema";
            }) output
          );
      };

      appsSchema = {
        version = 1;
        doc = ''
          The `apps` output provides commands available via `nix run`.
        '';
        roles.nix-run = { };
        appendSystem = true;
        defaultAttrPath = [ "default" ];
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (system: apps: {
              forSystems = [ system ];
              children = builtins.mapAttrs (appName: app: mkApp system app) apps;
            }) output
          );
      };

      defaultAppSchema = {
        version = 1;
        doc = ''
          **DEPRECATED**. Use `apps.<system>.default` instead.
        '';
        roles.nix-run = { };
        appendSystem = true;
        defaultAttrPath = [ ];
        inventory = output: self.lib.mkChildren (builtins.mapAttrs mkApp output);
      };

      packagesSchema = {
        version = 1;
        doc = ''
          The `packages` flake output contains packages that can be added to a shell using `nix shell`.
        '';
        roles.nix-build = { };
        roles.nix-run = { };
        roles.nix-develop = { };
        roles.nix-search = { };
        appendSystem = true;
        defaultAttrPath = [ "default" ];
        inventory = self.lib.derivationsInventory "package" false;
      };

      defaultPackageSchema = {
        version = 1;
        doc = ''
          **DEPRECATED**. Use `packages.<system>.default` instead.
        '';
        roles.nix-build = { };
        roles.nix-run = { };
        roles.nix-develop = { };
        roles.nix-search = { };
        appendSystem = true;
        defaultAttrPath = [ ];
        inventory = singleDerivationInventory "package" false;
      };

      ociImagesSchema = {
        version = 1;
        doc = ''
          The `ociImages` flake output contains derivations that build valid Open Container Initiative images.
        '';
        inventory = self.lib.derivationsInventory "OCI image" false;
      };

      legacyPackagesSchema = {
        version = 1;
        doc = ''
          The `legacyPackages` flake output is similar to `packages` but different in that it can be nested and thus contain attribute sets that contain more packages.
          Since enumerating packages in nested attribute sets can be inefficient, you should favor `packages` over `legacyPackages`.
        '';
        roles.nix-build = { };
        roles.nix-run = { };
        roles.nix-search = { };
        roles.nix-develop = { };
        appendSystem = true;
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (systemType: packagesForSystem: {
              forSystems = [ systemType ];
              isLegacy = true;
              children =
                let
                  recurse =
                    prefix: attrs:
                    builtins.mapAttrs (
                      attrName: attrs:
                      # Necessary to deal with `AAAAAASomeThingsFailToEvaluate` etc. in Nixpkgs.
                      self.lib.try (
                        if attrs.type or null == "derivation" then
                          {
                            forSystems = [ attrs.system ];
                            shortDescription = attrs.meta.description or "";
                            derivationAttrPath = [ ];
                            what = "package";
                          }
                        else
                        # Recurse at the first and second levels, or if the
                        # recurseForDerivations attribute if set.
                        if attrs.recurseForDerivations or false then
                          {
                            children = recurse (prefix + attrName + ".") attrs;
                          }
                        else
                          {
                            what = "unknown";
                          }
                      ) (throw "failed")
                    ) attrs;
                in
                # The top-level cannot be a derivation.
                assert packagesForSystem.type or null != "derivation";
                recurse (systemType + ".") packagesForSystem;
            }) output
          );
      };

      checksSchema = {
        version = 1;
        doc = ''
          The `checks` flake output contains derivations that will be built by `nix flake check`.
        '';
        # FIXME: add role
        inventory = self.lib.derivationsInventory "CI test" true;
      };

      devShellsSchema = {
        version = 1;
        doc = ''
          The `devShells` flake output contains derivations that provide a development environment for `nix develop`.
        '';
        roles.nix-develop = { };
        appendSystem = true;
        defaultAttrPath = [ "default" ];
        inventory = self.lib.derivationsInventory "development environment" false;
      };

      devShellSchema = {
        version = 1;
        doc = ''
          **DEPRECATED**. Use `devShells.<system>.default` instead.
        '';
        roles.nix-develop = { };
        appendSystem = true;
        defaultAttrPath = [ ];
        inventory = singleDerivationInventory "development environment" false;
      };

      formatterSchema = {
        version = 1;
        doc = ''
          The `formatter` output specifies the package to use to format the project.
        '';
        roles.nix-fmt = { };
        appendSystem = true;
        defaultAttrPath = [ ];
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (system: formatter: {
              forSystems = [ system ];
              shortDescription = formatter.meta.description or "";
              derivationAttrPath = [ ];
              what = "formatter";
              isFlakeCheck = false;
            }) output
          );
      };

      templatesSchema = {
        version = 1;
        doc = ''
          The `templates` output provides project templates.
        '';
        roles.nix-template = { };
        defaultAttrPath = [ "default" ];
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (templateName: template: {
              shortDescription = template.description or "";
              evalChecks.isValidTemplate =
                template ? path
                && builtins.isPath template.path
                && template ? description
                && builtins.isString template.description;
              what = "template";
            }) output
          );
      };

      hydraJobsSchema = {
        version = 1;
        doc = ''
          The `hydraJobs` flake output defines derivations to be built by the Hydra continuous integration system.
        '';
        allowIFD = false;
        inventory =
          output:
          let
            recurse =
              prefix: attrs:
              self.lib.mkChildren (
                builtins.mapAttrs (
                  attrName: attrs:
                  if attrs.type or null == "derivation" then
                    {
                      forSystems = [ attrs.system ];
                      shortDescription = attrs.meta.description or "";
                      derivationAttrPath = [ ];
                      what = "Hydra CI test";
                    }
                  else
                    recurse (prefix + attrName + ".") attrs
                ) attrs
              );
          in
          # The top-level cannot be a derivation.
          assert output.type or null != "derivation";
          recurse "" output;
      };

      overlaysSchema = {
        version = 1;
        doc = ''
          The `overlays` flake output defines ["overlays"](https://nixos.org/manual/nixpkgs/stable/#chap-overlays) that can be plugged into Nixpkgs.
          Overlays add additional packages or modify or replace existing packages.
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (overlayName: overlay: {
              what = "Nixpkgs overlay";
              evalChecks.isOverlay =
                # FIXME: should try to apply the overlay to an actual
                # Nixpkgs.  But we don't have access to a nixpkgs
                # flake here. Maybe this schema should be moved to the
                # nixpkgs flake, where it does have access.
                if !builtins.isFunction overlay then
                  throw "Overlay is not a function. It should be structured like: `final: previous: { /* ... */ }`."
                else
                  true;
            }) output
          );
      };

      nixosConfigurationsSchema = {
        version = 1;
        doc = ''
          The `nixosConfigurations` flake output defines [NixOS system configurations](https://nixos.org/manual/nixos/stable/#ch-configuration).
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (configName: machine: {
              what = "NixOS configuration";
              derivationAttrPath = [
                "config"
                "system"
                "build"
                "toplevel"
              ];
              forSystems = [ machine.pkgs.stdenv.system ];
            }) output
          );
      };

      nixosModulesSchema = {
        version = 1;
        doc = ''
          The `nixosModules` flake output defines importable [NixOS modules](https://nixos.org/manual/nixos/stable/#sec-writing-modules).
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (moduleName: module: {
              what = "NixOS module";
              evalChecks.isFunctionOrAttrs = checkModule module;
            }) output
          );
      };

      homeConfigurationsSchema = {
        version = 1;
        doc = ''
          The `homeConfigurations` flake output defines [Home Manager configurations](https://github.com/nix-community/home-manager).
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (configName: this: {
              what = "Home Manager configuration";
              derivationAttrPath = [ "activationPackage" ];
              forSystems = [ this.activationPackage.system ];
            }) output
          );
      };

      homeModulesSchema = {
        version = 1;
        doc = ''
          The `homeModules` flake output defines importable [Home Manager](https://github.com/nix-community/home-manager) modules.
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (moduleName: module: {
              what = "Home Manager module";
              evalChecks.isFunctionOrAttrs = checkModule module;
            }) output
          );
      };

      darwinConfigurationsSchema = {
        version = 1;
        doc = ''
          The `darwinConfigurations` flake output defines [nix-darwin configurations](https://github.com/nix-darwin/nix-darwin).
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (configName: this: {
              what = "nix-darwin configuration";
              derivationAttrPath = [ "system" ];
              forSystems = [ this.system.system ];
            }) output
          );
      };

      darwinModulesSchema = {
        version = 1;
        doc = ''
          The `darwinModules` flake output defines importable [nix-darwin modules](https://github.com/nix-darwin/nix-darwin).
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (moduleName: module: {
              what = "nix-darwin module";
              evalChecks.isFunctionOrAttrs = checkModule module;
            }) output
          );
      };

      bundlersSchema = {
        version = 1;
        doc = ''
          The `bundlers` flake output defines ["bundlers"](https://nix.dev/manual/nix/latest/command-ref/new-cli/nix3-bundle) that transform derivation outputs into other formats, typically self-extracting executables or container images.
        '';
        roles.nix-bundler = { };
        appendSystem = true;
        defaultAttrPath = [ "default" ];
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (
              system: bundlers:
              let
                forSystems = [ system ];
              in
              {
                inherit forSystems;
                children = builtins.mapAttrs (bundlerName: bundler: {
                  inherit forSystems;
                  evalChecks.isValidBundler = builtins.isFunction bundler;
                  what = "bundler";
                }) bundlers;
              }
            ) output
          );
      };

    in

    {
      # Helper functions
      lib = {
        try =
          e: default:
          let
            res = builtins.tryEval e;
          in
          if res.success then res.value else default;

        mkChildren = children: { inherit children; };

        derivationsInventory =
          what: isFlakeCheck: output:
          self.lib.mkChildren (
            builtins.mapAttrs (systemType: packagesForSystem: {
              forSystems = [ systemType ];
              children = builtins.mapAttrs (
                packageName: mkPackage isFlakeCheck what systemType
              ) packagesForSystem;
            }) output
          );
      };

      exportedSchemas = {
        schemas = schemasSchema ''
          The `schemas` flake output is used to define and document flake outputs.
          For the expected format, consult the Nix manual.
        '';
        exportedSchemas = schemasSchema ''
          The `exportedSchemas` flake output is used to define flake schemas that you
          intend for other flakes to use.
        '';
        apps = appsSchema;
        defaultApp = defaultAppSchema;
        packages = packagesSchema;
        defaultPackage = defaultPackageSchema;
        legacyPackages = legacyPackagesSchema;
        checks = checksSchema;
        devShells = devShellsSchema;
        devShell = devShellSchema;
        formatter = formatterSchema;
        templates = templatesSchema;
        hydraJobs = hydraJobsSchema;
        overlays = overlaysSchema;
        nixosConfigurations = nixosConfigurationsSchema;
        nixosModules = nixosModulesSchema;
        homeConfigurations = homeConfigurationsSchema;
        homeModules = homeModulesSchema;
        darwinConfigurations = darwinConfigurationsSchema;
        darwinModules = darwinModulesSchema;
        ociImages = ociImagesSchema;
        bundlers = bundlersSchema;
      };

      schemas = self.exportedSchemas;
    };
}
