#pragma once
/// @file Private header defining LocalStore::State::Stmts.

#include "nix/store/local-store.hh"

namespace nix {

struct LocalStore::State::Stmts
{
    /* Some precompiled SQLite statements. */
    SQLiteStmt RegisterValidPath;
    SQLiteStmt UpdatePathInfo;
    SQLiteStmt AddReference;
    SQLiteStmt QueryPathInfo;
    SQLiteStmt QueryReferences;
    SQLiteStmt QueryReferrers;
    SQLiteStmt InvalidatePath;
    SQLiteStmt AddDerivationOutput;
    SQLiteStmt RegisterRealisedOutput;
    SQLiteStmt UpdateRealisedOutput;
    SQLiteStmt QueryValidDerivers;
    SQLiteStmt QueryDerivationOutputs;
    SQLiteStmt QueryRealisedOutput;
    SQLiteStmt QueryAllRealisedOutputs;
    SQLiteStmt QueryPathFromHashPart;
    SQLiteStmt QueryValidPaths;
    /* Like RegisterValidPath but handles the repair case via upsert */
    SQLiteStmt RegisterOrUpdateValidPath;
    /* Derivations-in-DB statements */
    SQLiteStmt InsertDerivation;
    SQLiteStmt InsertDerivationOutput;
    SQLiteStmt InsertDerivationInput;
    SQLiteStmt InsertDerivationInputSrc;
    SQLiteStmt InsertDerivationEnv;
    SQLiteStmt InsertDerivationStructuredAttr;
    SQLiteStmt QueryDerivation;
    SQLiteStmt QueryDerivationOutputsV2;
    SQLiteStmt QueryDerivationInputs;
    SQLiteStmt QueryDerivationInputSrcs;
    SQLiteStmt QueryDerivationEnvs;
    SQLiteStmt QueryDerivationStructuredAttrs;
};

} // namespace nix
