{
  description = "Schemas for well-known Nix flake output types";

  outputs =
    { self }:
    let
      mapAttrsToList = f: attrs: map (name: f name attrs.${name}) (builtins.attrNames attrs);

      checkDerivation =
        drv: drv.type or null == "derivation" && drv ? drvPath && drv ? name && builtins.isString drv.name;

      checkModule = module: builtins.isAttrs module || builtins.isFunction module;

      schemasSchema = {
        version = 1;
        doc = ''
          The `schemas` flake output is used to define and document flake outputs.
          For the expected format, consult the Nix manual.
        '';
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
            builtins.mapAttrs (
              system: apps:
              let
                forSystems = [ system ];
              in
              {
                inherit forSystems;
                children = builtins.mapAttrs (appName: app: {
                  inherit forSystems;
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
                }) apps;
              }
            ) output
          );
      };

      packagesSchema = {
        version = 1;
        doc = ''
          The `packages` flake output contains packages that can be added to a shell using `nix shell`.
        '';
        roles.nix-build = { };
        roles.nix-run = { };
        roles.nix-develop = { };
        appendSystem = true;
        defaultAttrPath = [ "default" ];
        inventory = self.lib.derivationsInventory "package" false;
      };

      dockerImagesSchema = {
        version = 1;
        doc = ''
          The `dockerImages` flake output contains derivations that build valid Docker images.
        '';
        inventory = self.lib.derivationsInventory "Docker image" false;
      };

      legacyPackagesSchema = {
        version = 1;
        doc = ''
          The `legacyPackages` flake output is similar to `packages` but different in that it can be nested and thus contain attribute sets that contain more packages.
          Since enumerating packages in nested attribute sets can be inefficient, you should favor `packages` over `legacyPackages`.
        '';
        roles.nix-build = { };
        roles.nix-run = { };
        appendSystem = true;
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (systemType: packagesForSystem: {
              forSystems = [ systemType ];
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
                            derivation = attrs;
                            evalChecks.isDerivation = checkDerivation attrs;
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
              derivation = formatter;
              evalChecks.isDerivation = checkDerivation formatter;
              what = "package";
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
                      derivation = attrs;
                      evalChecks.isDerivation = checkDerivation attrs;
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
                  throw "overlay is not a function, but a set instead"
                else
                  builtins.isAttrs (overlay { } { });
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
              derivation = machine.config.system.build.toplevel;
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
              derivation = this.activationPackage;
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
          The `darwinConfigurations` flake output defines [nix-darwin configurations](https://github.com/LnL7/nix-darwin).
        '';
        inventory =
          output:
          self.lib.mkChildren (
            builtins.mapAttrs (configName: this: {
              what = "nix-darwin configuration";
              derivation = this.system;
              forSystems = [ this.system.system ];
            }) output
          );
      };

      darwinModulesSchema = {
        version = 1;
        doc = ''
          The `darwinModules` flake output defines importable [nix-darwin modules](https://github.com/LnL7/nix-darwin).
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
          The `bundlers` flake output defines ["bundlers"](https://nix.dev/manual/nix/2.26/command-ref/new-cli/nix3-bundle) that transform derivation outputs into other formats, typically self-extracting executables or container images.
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
              children = builtins.mapAttrs (packageName: package: {
                forSystems = [ systemType ];
                shortDescription = package.meta.description or "";
                derivation = package;
                evalChecks.isDerivation = checkDerivation package;
                inherit what;
                isFlakeCheck = isFlakeCheck;
              }) packagesForSystem;
            }) output
          );
      };

      # FIXME: distinguish between available and active schemas?
      schemas.schemas = schemasSchema;
      schemas.apps = appsSchema;
      schemas.packages = packagesSchema;
      schemas.legacyPackages = legacyPackagesSchema;
      schemas.checks = checksSchema;
      schemas.devShells = devShellsSchema;
      schemas.formatter = formatterSchema;
      schemas.templates = templatesSchema;
      schemas.hydraJobs = hydraJobsSchema;
      schemas.overlays = overlaysSchema;
      schemas.nixosConfigurations = nixosConfigurationsSchema;
      schemas.nixosModules = nixosModulesSchema;
      schemas.homeConfigurations = homeConfigurationsSchema;
      schemas.homeModules = homeModulesSchema;
      schemas.darwinConfigurations = darwinConfigurationsSchema;
      schemas.darwinModules = darwinModulesSchema;
      schemas.dockerImages = dockerImagesSchema;
      schemas.bundlers = bundlersSchema;
    };
}
