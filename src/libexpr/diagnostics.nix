let
  kinds = {
    warning = "Warning";
    error = "Error";
  };
  tags = {
    unnecessary = "Unnecessary";
    deprecated = "Deprecated";
  };
in
[
  {
    name = "PathHasTrailingSlash";
    level = kinds.error;
    message = "path has a trailing slash";
  }
  rec {
    name = "InvalidInteger";
    level = kinds.error;
    message = "invalid integer '%1%'";
    body = "std::string text;";
    ctor = ''Diag${name}(PosIdx p, std::string text): Diag(p), text(std::move(text)) { }'';
    format = "return hintfmt(\"${message}\", text).str();";
  }
  rec {
    name = "InvalidFloat";
    level = kinds.error;
    message = "invalid float '%1%'";
    body = "std::string text;";
    ctor = ''Diag${name}(PosIdx p, std::string text): Diag(p), text(std::move(text)) { }'';
    format = "return hintfmt(\"${message}\", text).str();";
  }
  {
    name = "DynamicAttrsInLet";
    level = kinds.error;
    message = "dynamic attributes not allowed in let";
  }
  {
    name = "URLLiteralsDisabled";
    level = kinds.error;
    message = "URL literals are disabled";
    tags = tags.deprecated;
  }
  {
    name = "URLLiterals";
    level = kinds.warning;
    message = "using deprecated URL literal syntax";
    tags = tags.deprecated;
  }
  rec {
    name = "HPathPure";
    level = kinds.error;
    message = "home-path '%s' can not be resolved in pure mode";
    body = "std::string text;";
    ctor = ''Diag${name}(PosIdx p, std::string text): Diag(p), text(std::move(text)) { }'';
    format = "return hintfmt(\"${message}\", text).str();";
  }
  {
    name = "InheritDynamic";
    level = kinds.error;
    message = "dynamic attributes not allowed in inherit";
  }
  rec {
    name = "Syntax";
    level = kinds.error;
    message = "";
    body = "std::string text;";
    ctor = ''Diag${name}(PosIdx p, std::string text): Diag(p), text(std::move(text)) { }'';
    format = "return text;";
  }
]
