#include <queue>

#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"
#include "nix/util/hash.hh"

#include "derivation-check.hh"

namespace nix {

void checkCAOutput(
    StoreDirConfig & store,
    const StorePath & drvPath,
    const DerivationOutput & outputSpec,
    const ValidPathInfo & info,
    const std::string & outputName)
{
    std::visit(
        overloaded{
            [&](const DerivationOutput::CAFixed & dof) {
                auto & wanted = dof.ca.hash;

                /* Check wanted hash */
                assert(info.ca);
                auto & got = info.ca->hash;
                if (wanted != got) {
                    throw BuildError(
                        BuildResult::Failure::HashMismatch,
                        "hash mismatch in fixed-output derivation '%s':\n  specified: %s\n     got:    %s",
                        store.printStorePath(drvPath),
                        wanted.to_string(HashFormat::SRI, true),
                        got.to_string(HashFormat::SRI, true));
                }
                if (!info.references.empty()) {
                    auto numViolations = info.references.size();
                    throw BuildError(
                        BuildResult::Failure::HashMismatch,
                        "fixed-output derivations must not reference store paths: '%s' references %d distinct paths, e.g. '%s'",
                        store.printStorePath(drvPath),
                        numViolations,
                        store.printStorePath(*info.references.begin()));
                }
            },
            [&](const DerivationOutput::CAFloating & dof) {
                if (!info.ca.has_value()) {
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "floating content-addressing derivation '%s' output '%s' (at '%s') was not content-addressed",
                        store.printStorePath(drvPath),
                        outputName,
                        store.printStorePath(info.path));
                }
                if (info.ca->method != dof.method) {
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "content-addressing derivation '%s' output '%s' (at '%s') was hashed with method '%s', expected '%s'",
                        store.printStorePath(drvPath),
                        outputName,
                        store.printStorePath(info.path),
                        info.ca->method.render(),
                        dof.method.render());
                }
                if (info.ca->hash.algo != dof.hashAlgo) {
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "content-addressing derivation '%s' output '%s' (at '%s') was hashed with algorithm '%s', expected '%s'",
                        store.printStorePath(drvPath),
                        outputName,
                        store.printStorePath(info.path),
                        printHashAlgo(info.ca->hash.algo),
                        printHashAlgo(dof.hashAlgo));
                }
            },
            [&](const DerivationOutput::Deferred & _) {},
            [&](const DerivationOutput::Impure & _) {},
            [&](const DerivationOutput::InputAddressed & _) {},
        },
        outputSpec.raw);
}

void checkOutputs(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const std::map<std::string, ValidPathInfo> & outputs)
{
    std::map<StorePath, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(output.second.path, output.second);

    for (auto & pair : outputs) {
        // We can't use auto destructuring here because
        // clang-tidy seems to complain about it.
        const std::string & outputName = pair.first;
        const auto & info = pair.second;

        auto * outputSpec = get(drv.outputs, outputName);
        if (!outputSpec) {
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' submitted unknown output '%s' (Valid outputs are [%s])",
                store.printStorePath(drvPath),
                outputName,
                concatMapStringsSep(", ", outputs, [](auto & o) { return o.first; }));
        }

        if (outputPathName(drv.name, outputName) != info.path.name()) {
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "derivation '%s' output '%s' (at '%s') was named '%s', expected '%s'",
                store.printStorePath(drvPath),
                outputName,
                store.printStorePath(info.path),
                info.path.name(),
                outputPathName(drv.name, outputName));
        }

        checkCAOutput(store, drvPath, outputSpec->output, info, outputName);

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const StorePath & path) {
            uint64_t closureSize = 0;
            StorePathSet pathsDone;
            std::queue<StorePath> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (!pathsDone.insert(path).second)
                    continue;

                auto i = outputsByPath.find(path);
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.references)
                        pathsLeft.push(ref);
                } else {
                    auto info = store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->references)
                        pathsLeft.push(ref);
                }
            }

            return std::make_pair(std::move(pathsDone), closureSize);
        };

        auto applyChecks = [&](const derivation::OutputChecks<StorePath> & checks) {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "path '%s' is too large at %d bytes; limit is %d bytes",
                    store.printStorePath(info.path),
                    info.narSize,
                    *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).second;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        store.printStorePath(info.path),
                        closureSize,
                        *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::set<DrvRef<StorePath>> & value, bool allowed, bool recursive) {
                /* Parse a list of reference specifiers.  Each element must
                   either be a store path, or the symbolic name of the output
                   of the derivation (such as `out'). */
                StorePathSet spec;
                for (auto & i : value) {
                    std::visit(
                        overloaded{
                            [&](const StorePath & path) { spec.insert(path); },
                            [&](const OutputName & refOutputName) {
                                if (auto output = get(outputs, refOutputName))
                                    spec.insert(output->path);
                                else {
                                    throw BuildError(
                                        BuildResult::Failure::OutputRejected,
                                        "derivation '%s' output check for '%s' contains output name '%s',"
                                        " but this is not a valid output of this derivation."
                                        " (Valid outputs are [%s].)",
                                        store.printStorePath(drvPath),
                                        outputName,
                                        refOutputName,
                                        concatMapStringsSep(", ", outputs, [](auto & o) { return o.first; }));
                                }
                            }},
                        i);
                }

                auto used = recursive ? getClosure(info.path).first : info.references;

                if (recursive && checks.ignoreSelfRefs)
                    used.erase(info.path);

                StorePathSet badPaths;

                for (auto & i : used)
                    if (allowed) {
                        if (!spec.count(i))
                            badPaths.insert(i);
                    } else {
                        if (spec.count(i))
                            badPaths.insert(i);
                    }

                if (!badPaths.empty()) {
                    std::string badPathsStr;
                    for (auto & i : badPaths) {
                        badPathsStr += "\n  ";
                        badPathsStr += store.printStorePath(i);
                    }
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "output '%s' is not allowed to refer to the following paths:%s",
                        store.printStorePath(info.path),
                        badPathsStr);
                }
            };

            /* Mandatory check: absent whitelist, and present but empty
               whitelist mean very different things. */
            if (auto & refs = checks.allowedReferences) {
                checkRefs(*refs, true, false);
            }
            if (auto & refs = checks.allowedRequisites) {
                checkRefs(*refs, true, true);
            }

            /* Optimization: don't need to do anything when
               disallowed and empty set. */
            if (!checks.disallowedReferences.empty()) {
                checkRefs(checks.disallowedReferences, false, false);
            }
            if (!checks.disallowedRequisites.empty()) {
                checkRefs(checks.disallowedRequisites, false, true);
            }
        };

        if (auto * outputWithOpts = get(drv.outputs, outputName); outputWithOpts && outputWithOpts->options.checks)
            applyChecks(*outputWithOpts->options.checks);
        else if (drv.options.allOutputChecks)
            applyChecks(*drv.options.allOutputChecks);
    }
}

} // namespace nix
