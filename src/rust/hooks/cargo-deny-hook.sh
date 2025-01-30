# shellcheck shell=bash

# Check with deny. Advisories are only checked on CI due to needing to fetch the db.
cargoDenyHook() {
    cargo deny --offline check bans licenses sources
}
