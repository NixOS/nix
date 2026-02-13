{ busybox }:

with import ./config.nix;

let
  drv1 = mkDerivation {
    name = "resource-management-1";
    shell = busybox;
    builder = ./simple.builder.sh;
    PATH = "";
    goodPath = path;
    requiredSystemFeatures = ["test:2"];
    meta.position = "${__curPos.file}:${toString __curPos.line}";
  };
  drv2 = mkDerivation {
    name = "resource-management-2";
    shell = busybox;
    builder = ./simple.builder.sh;
    PATH = "";
    goodPath = path;
    requiredSystemFeatures = ["test:2"];
    meta.position = "${__curPos.file}:${toString __curPos.line}";
  };
  drv3 = mkDerivation {
    name = "resource-management-3";
    shell = busybox;
    builder = ./simple.builder.sh;
    PATH = "";
    goodPath = path;
    requiredSystemFeatures = ["test:2"];
    meta.position = "${__curPos.file}:${toString __curPos.line}";
  };
  drv4 = mkDerivation {
    name = "resource-management-4";
    shell = busybox;
    builder = ./simple.builder.sh;
    PATH = "";
    goodPath = path;
    requiredSystemFeatures = ["test:2"];
    meta.position = "${__curPos.file}:${toString __curPos.line}";
  };
in mkDerivation {
  name = "resource-management";
  shell = busybox;
  builder = ./simple.builder.sh;
  PATH = "";
  goodPath = path;
  DRVS = "${drv1}${drv2}${drv3}${drv4}";
  meta.position = "${__curPos.file}:${toString __curPos.line}";
}
