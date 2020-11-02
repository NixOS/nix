with builtins;
with import ./utils.nix;

let

  showCommand =
    { command, section, def }:
    "${section} Name\n\n"
    + "`${command}` - ${def.description}\n\n"
    + "${section} Synopsis\n\n"
    + showSynopsis { inherit command; args = def.args; }
    + (if def ? doc
       then "${section} Description\n\n" + def.doc + "\n\n"
       else "")
    + (let s = showFlags def.flags; in
       if s != ""
       then "${section} Flags\n\n${s}"
       else "")
    + (if def.examples or [] != []
       then
         "${section} Examples\n\n"
         + concatStrings (map ({ description, command }: "${description}\n\n```console\n${command}\n```\n\n") def.examples)
       else "")
    + (if def.commands or [] != []
       then concatStrings (
         map (name:
           "# Subcommand `${command} ${name}`\n\n"
           + showCommand { command = command + " " + name; section = "##"; def = def.commands.${name}; })
           (attrNames def.commands))
       else "");

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

in

command:

showCommand { command = "nix"; section = "#"; def = command; }
