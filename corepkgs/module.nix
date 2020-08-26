let

  showPos = pos:
    if pos == null
    then "<unknown location>"
    else "${pos.file}:${toString pos.line}:${toString pos.column}";

  getAnyPos = attrs:
    builtins.foldl' (prev: name: if prev == null then builtins.unsafeGetAttrPos name attrs else prev) null (builtins.attrNames attrs);

in

{ description ? null, extends ? [], options ? {}, config ? ({ config }: {}) } @ inArgs:

let thisModule = rec {
  type = "module";

  _module = {
    inherit description extends options config;
  };

  _allModules = [thisModule] ++ builtins.concatLists (map (mod: assert mod.type or "<untyped>" == "module"; mod._allModules) extends);

  _allOptions = builtins.foldl' (xs: mod: xs // mod._module.options) {} _allModules;

  _allConfigs = map (mod: mod._module.config { config = final; }) _allModules;

  _allDefinitions = builtins.mapAttrs (name: value: map (x: x) (builtins.catAttrs name _allConfigs)) _allOptions;

  final = builtins.mapAttrs
    (name: defs:
      if defs == []
      then
        _allOptions.${name}.default
          or (throw "Option '${name}' is not defined by module at ${showPos (getAnyPos inArgs)} and has no default value.")
      else
        # FIXME: support merge functions.
        if builtins.isList (builtins.head defs)
        then builtins.concatLists defs
        else
          if builtins.isAttrs (builtins.head defs)
          then builtins.foldl' (xs: ys: xs // ys) {} defs
          else builtins.head defs)
    _allDefinitions;

}; in thisModule
