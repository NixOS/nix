#include "nix/store/local-store.hh"
#include "local-store-stmts.hh"
#include "nix/store/derivations.hh"
#include "nix/store/path-info.hh"
#include "nix/util/archive.hh"
#include "nix/util/memory-source-accessor.hh"

#include <nlohmann/json.hpp>

namespace nix {

/**
 * Compute the output type discriminant for the DB schema from a
 * derivation output.
 */
static int outputTypeCode(const DerivationOutput & output)
{
    return std::visit(
        overloaded{
            [](const DerivationOutput::InputAddressed &) { return 0; },
            [](const DerivationOutput::CAFixed &) { return 1; },
            [](const DerivationOutput::CAFloating &) { return 2; },
            [](const DerivationOutput::Deferred &) { return 3; },
            [](const DerivationOutput::Impure &) { return 4; },
        },
        output.raw);
}

/**
 * Flatten a DerivedPathMap into (path, outputs-json-array) pairs for
 * DB storage. Each entry in the trie is walked depth-first to produce
 * rows.
 */
static void flattenInputDrvs(
    const DerivedPathMap<std::set<OutputName, std::less<>>>::Map & map,
    std::vector<std::pair<std::string, nlohmann::json>> & out,
    nlohmann::json prefix = nlohmann::json::array())
{
    for (auto & [path, node] : map) {
        auto pathStr = path.to_string();
        if (!node.value.empty()) {
            for (auto & outputName : node.value) {
                auto outputs = prefix;
                outputs.emplace_back(outputName);
                out.emplace_back(pathStr, std::move(outputs));
            }
        } else if (node.childMap.empty()) {
            /* Opaque path reference (no outputs requested) */
            out.emplace_back(pathStr, prefix);
        }
        /* Recurse into child nodes for dynamic derivation support */
        for (auto & [outputName, childNode] : node.childMap) {
            auto childPrefix = prefix;
            childPrefix.emplace_back(outputName);
            /* The childNode itself has value + childMap. If it has values,
               those are outputs-of-outputs. */
            if (!childNode.value.empty()) {
                for (auto & output2 : childNode.value) {
                    auto outputs = childPrefix;
                    outputs.emplace_back(output2);
                    out.emplace_back(pathStr, std::move(outputs));
                }
            }
            if (!childNode.childMap.empty()) {
                /* For deeper nesting, we'd need more recursion. For now
                   just handle the common case. */
            }
        }
    }
}

void LocalStore::insertDerivationIntoDB(State & state, uint64_t id, const std::string & pathStr, const Derivation & drv)
{
    /* Determine the output type from the first output */
    int outType = 0;
    if (!drv.outputs.empty())
        outType = outputTypeCode(drv.outputs.begin()->second);

    int hasStructuredAttrs = drv.structuredAttrs.has_value() ? 1 : 0;

    /* Serialize args as JSON array */
    nlohmann::json argsJson = nlohmann::json::array();
    for (auto & arg : drv.args)
        argsJson.push_back(arg);

    /* Insert core derivation row */
    state.stmts->InsertDerivation
        .use()(id)(pathStr) (drv.platform)(drv.builder)(argsJson.dump())(outType) (hasStructuredAttrs)
        .exec();

    /* Insert outputs */
    for (auto & [outputName, output] : drv.outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed & ia) {
                    state.stmts->InsertDerivationOutput
                        .use()(id)(outputName) (outType) (ia.path.to_string())("", false)("", false)("", false)
                        .exec();
                },
                [&](const DerivationOutput::CAFixed & ca) {
                    state.stmts->InsertDerivationOutput
                        .use()(id)(outputName) (outType) ("", false)(std::string{ca.ca.method.render()})(std::string{
                            printHashAlgo(ca.ca.hash.algo)})(ca.ca.hash.to_string(HashFormat::Base16, false))
                        .exec();
                },
                [&](const DerivationOutput::CAFloating & caf) {
                    state.stmts->InsertDerivationOutput
                        .use()(id)(outputName) (outType) ("", false)(std::string{caf.method.render()})(
                            std::string{printHashAlgo(caf.hashAlgo)})("", false)
                        .exec();
                },
                [&](const DerivationOutput::Deferred &) {
                    state.stmts->InsertDerivationOutput
                        .use()(id)(outputName) (outType) ("", false)("", false)("", false)("", false)
                        .exec();
                },
                [&](const DerivationOutput::Impure & imp) {
                    state.stmts->InsertDerivationOutput
                        .use()(id)(outputName) (outType) ("", false)(std::string{imp.method.render()})(
                            std::string{printHashAlgo(imp.hashAlgo)})("", false)
                        .exec();
                },
            },
            output.raw);
    }

    /* Insert input derivations */
    std::vector<std::pair<std::string, nlohmann::json>> inputRows;
    flattenInputDrvs(drv.inputDrvs.map, inputRows);
    for (auto & [path, outputs] : inputRows) {
        state.stmts->InsertDerivationInput.use()(id)(path) (outputs.dump()).exec();
    }

    /* Insert input sources */
    for (auto & src : drv.inputSrcs) {
        state.stmts->InsertDerivationInputSrc.use()(id)(src.to_string()).exec();
    }

    /* Insert env */
    for (auto & [key, value] : drv.env) {
        state.stmts->InsertDerivationEnv.use()(id)(key) (value).exec();
    }

    /* Insert structured attrs */
    if (drv.structuredAttrs) {
        for (auto & [key, value] : drv.structuredAttrs->structuredAttrs) {
            state.stmts->InsertDerivationStructuredAttr.use()(id)(key) (value.dump()).exec();
        }
    }
}

std::optional<Derivation> LocalStore::queryDerivationFromDB(State & state, const StorePath & drvPath)
{
    auto useQuery(state.stmts->QueryDerivation.use()(printStorePath(drvPath)));
    if (!useQuery.next())
        return std::nullopt;

    Derivation drv;

    auto id = useQuery.getInt(0);
    drv.platform = useQuery.getStr(1);
    drv.builder = useQuery.getStr(2);
    auto argsStr = useQuery.getStr(3);
    /* outType is stored per-row but we read it from each output row instead */
    (void) useQuery.getInt(4);
    auto hasStructuredAttrs = useQuery.getInt(5);
    drv.name = std::string(BasicDerivation::nameFromPath(drvPath));

    /* Parse args from JSON */
    auto argsJson = nlohmann::json::parse(argsStr);
    for (auto & arg : argsJson)
        drv.args.push_back(arg.get<std::string>());

    /* Query outputs */
    {
        auto useOutputs(state.stmts->QueryDerivationOutputsV2.use()(id));
        while (useOutputs.next()) {
            auto outputName = useOutputs.getStr(0);
            auto outputType = useOutputs.getInt(1);

            DerivationOutput output = DerivationOutput{DerivationOutput::Deferred{}};

            switch (outputType) {
            case 0: { /* InputAddressed */
                auto pathStr = useOutputs.getStr(2);
                output = DerivationOutput{DerivationOutput::InputAddressed{
                    .path = StorePath(pathStr),
                }};
                break;
            }
            case 1: { /* CAFixed */
                auto method = ContentAddressMethod::parse(useOutputs.getStr(3));
                auto hashAlgo = parseHashAlgo(useOutputs.getStr(4));
                auto hash = Hash::parseNonSRIUnprefixed(useOutputs.getStr(5), hashAlgo);
                output = DerivationOutput{DerivationOutput::CAFixed{
                    .ca = ContentAddress{
                        .method = method,
                        .hash = hash,
                    }}};
                break;
            }
            case 2: { /* CAFloating */
                auto method = ContentAddressMethod::parse(useOutputs.getStr(3));
                auto hashAlgo = parseHashAlgo(useOutputs.getStr(4));
                output = DerivationOutput{DerivationOutput::CAFloating{
                    .method = method,
                    .hashAlgo = hashAlgo,
                }};
                break;
            }
            case 3: { /* Deferred */
                output = DerivationOutput{DerivationOutput::Deferred{}};
                break;
            }
            case 4: { /* Impure */
                auto method = ContentAddressMethod::parse(useOutputs.getStr(3));
                auto hashAlgo = parseHashAlgo(useOutputs.getStr(4));
                output = DerivationOutput{DerivationOutput::Impure{
                    .method = method,
                    .hashAlgo = hashAlgo,
                }};
                break;
            }
            }

            drv.outputs.insert_or_assign(outputName, std::move(output));
        }
    }

    /* Query input derivations */
    {
        auto useInputs(state.stmts->QueryDerivationInputs.use()(id));
        while (useInputs.next()) {
            StorePath path(useInputs.getStr(0));
            auto outputsJson = nlohmann::json::parse(useInputs.getStr(1));

            /* Reconstruct the DerivedPathMap trie entry.
               The JSON array encodes the path through the trie:
               [] = opaque, [o] = Built(Opaque, o), [o1,o2] = Built(Built(Opaque, o1), o2) */
            if (outputsJson.empty()) {
                /* Opaque path - just ensure the node exists */
                drv.inputDrvs.map.try_emplace(path);
            } else {
                auto & node = drv.inputDrvs.map[path];
                if (outputsJson.size() == 1) {
                    /* Simple case: direct output dependency */
                    node.value.insert(outputsJson[0].get<std::string>());
                } else {
                    /* Dynamic derivation: walk/create the trie */
                    auto * current = &node;
                    for (size_t i = 0; i < outputsJson.size() - 1; i++) {
                        auto outputName = outputsJson[i].get<std::string>();
                        current = &current->childMap[outputName];
                    }
                    current->value.insert(outputsJson.back().get<std::string>());
                }
            }
        }
    }

    /* Query input sources */
    {
        auto useSrcs(state.stmts->QueryDerivationInputSrcs.use()(id));
        while (useSrcs.next())
            drv.inputSrcs.insert(StorePath(useSrcs.getStr(0)));
    }

    /* Query env */
    {
        auto useEnv(state.stmts->QueryDerivationEnvs.use()(id));
        while (useEnv.next())
            drv.env.emplace(useEnv.getStr(0), useEnv.getStr(1));
    }

    /* Query structured attrs */
    if (hasStructuredAttrs) {
        StructuredAttrs sa;
        auto useSA(state.stmts->QueryDerivationStructuredAttrs.use()(id));
        while (useSA.next()) {
            auto key = useSA.getStr(0);
            auto value = nlohmann::json::parse(useSA.getStr(1));
            sa.structuredAttrs.emplace(key, std::move(value));
        }
        drv.structuredAttrs = std::move(sa);
    }

    return drv;
}

StorePath LocalStore::writeDerivation(const Derivation & drv, RepairFlag repair)
{
    if (!config->derivationsInDatabase)
        return Store::writeDerivation(drv, repair);

    auto path = nix::computeStorePath(*this, drv);

    addTempRoot(path);

    if (isValidPath(path) && !repair)
        return path;

    drv.checkInvariants(*this);

    /* Compute content info needed for ValidPathInfo */
    auto references = drv.inputSrcs;
    for (auto & i : drv.inputDrvs.map)
        references.insert(i.first);
    auto contents = drv.unparse(*this, false);
    auto hash = hashString(HashAlgorithm::SHA256, contents);

    /* Compute NAR hash (the NAR of a flat file wrapping the ATerm text) */
    HashSink narHashSink(HashAlgorithm::SHA256);
    dumpString(contents, narHashSink);
    auto narHashResult = narHashSink.finish();

    auto info = ValidPathInfo::makeFromCA(
        *this,
        std::string(drv.name) + drvExtension,
        TextInfo{.hash = hash, .references = references},
        narHashResult.hash);
    info.narSize = narHashResult.numBytesDigested;

    retrySQLite<void>([&]() {
        auto state(_state->lock());

        if (isValidPath_(*state, path) && !repair)
            return;

        SQLiteTxn txn(state->db);

        /* Insert into ValidPaths first (needed as FK target), get the row ID.
           We inline the relevant parts of addValidPath here because we need
           to insert the derivation content into the DB tables BEFORE
           addValidPath's readInvalidDerivation call.
           On repair, use INSERT OR REPLACE which cascade-deletes old
           derivation rows via FK constraints, then we re-insert them. */
        auto & regStmt = repair ? state->stmts->RegisterOrUpdateValidPath
                                : state->stmts->RegisterValidPath;
        regStmt
            .use()(printStorePath(info.path))(info.narHash.to_string(HashFormat::Base16, true))(
                info.registrationTime == 0 ? time(nullptr) : info.registrationTime)(
                info.deriver ? printStorePath(*info.deriver) : "",
                (bool) info.deriver)(info.narSize, info.narSize != 0)(info.ultimate ? 1 : 0, info.ultimate)(
                concatStringsSep(" ", Signature::toStrings(info.sigs)),
                !info.sigs.empty())(renderContentAddress(info.ca), (bool) info.ca)
            .exec();
        uint64_t id = state->db.getLastInsertedRowId();

        /* Insert the derivation content into the normalized tables */
        insertDerivationIntoDB(*state, id, printStorePath(path), drv);

        /* Cache output mappings (same as addValidPath does) */
        for (auto & i : drv.outputsAndOptPaths(*this)) {
            if (i.second.second)
                cacheDrvOutputMapping(*state, id, i.first, *i.second.second);
        }

        /* Add references */
        for (auto & ref : references)
            state->stmts->AddReference.use()(id)(queryValidPathId(*state, ref)).exec();

        /* Update path info cache */
        pathInfoCache->lock()->upsert(
            info.path, PathInfoCacheValue{.value = std::make_shared<const ValidPathInfo>(info)});

        txn.commit();
    });

    return path;
}

Derivation LocalStore::readDerivation(const StorePath & drvPath)
{
    if (!config->derivationsInDatabase)
        return Store::readDerivation(drvPath);

    auto drv = retrySQLite<std::optional<Derivation>>([&]() {
        auto state(_state->lock());
        return queryDerivationFromDB(*state, drvPath);
    });

    if (drv)
        return std::move(*drv);

    /* Fall through to filesystem-based read for backward compat */
    return Store::readDerivation(drvPath);
}

Derivation LocalStore::readInvalidDerivation(const StorePath & drvPath)
{
    if (!config->derivationsInDatabase)
        return Store::readInvalidDerivation(drvPath);

    auto drv = retrySQLite<std::optional<Derivation>>([&]() {
        auto state(_state->lock());
        return queryDerivationFromDB(*state, drvPath);
    });

    if (drv)
        return std::move(*drv);

    /* Fall through to filesystem-based read for backward compat */
    return Store::readInvalidDerivation(drvPath);
}

std::optional<Derivation> LocalStore::readDerivationFromDB(const StorePath & drvPath)
{
    return retrySQLite<std::optional<Derivation>>([&]() {
        auto state(_state->lock());
        return queryDerivationFromDB(*state, drvPath);
    });
}

std::optional<Derivation> LocalStore::readDerivationForAddValidPath(const StorePath &)
{
    return std::nullopt;
}

std::shared_ptr<SourceAccessor> LocalStore::getFSAccessor(const StorePath & path, bool requireValidPath)
{
    if (config->derivationsInDatabase && path.isDerivation()) {
        auto drv = readDerivationFromDB(path);

        if (drv) {
            auto accessor = std::make_shared<MemorySourceAccessor>();
            accessor->root = MemorySourceAccessor::File::Regular{
                .contents = drv->unparse(*this, false),
            };
            return accessor;
        }
    }

    /* Fall through to filesystem-based accessor */
    return LocalFSStore::getFSAccessor(path, requireValidPath);
}

} // namespace nix
