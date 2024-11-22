#!/usr/bin/env bash

source ./common.sh

createFlake1
repoDir="$flake1Dir"

# Check that a flakeref without a query is accepted correctly.
expectStderr 0 nix --offline build --dry-run "git+file://$repoDir#foo"

# Check that a flakeref with a good query is accepted correctly.
expectStderr 0 nix --offline build --dry-run "git+file://$repoDir?foo=bar#foo"

# Check that we get the dubious query warning, when passing in a query without an equal sign.
expectStderr 0 nix --offline build --dry-run "git+file://$repoDir?bar#foo" \
  | grepQuiet "warning: dubious URI query 'bar' is missing equal sign '=', ignoring"

# Check that the anchor (#) is taken as a whole, not split, and throws an error.
expectStderr 1 nix --offline build --dry-run "git+file://$repoDir#foo?bar" \
  | grepQuiet "error: flake 'git+file://$repoDir' does not provide attribute 'packages.$system.foo?bar', 'legacyPackages.$system.foo?bar' or 'foo?bar'"

# Check that a literal `?` in the query  doesn't print dubious query warning.
expectStderr 0 nix --offline build --dry-run "git+file://$repoDir?#foo" \
  | grepInverse "warning: dubious URI query "

# Check that a literal `?=` in the query doesn't print dubious query warning.
expectStderr 0 nix --offline build --dry-run "git+file://$repoDir?=#foo" \
  | grepInverse "warning: dubious URI query "
