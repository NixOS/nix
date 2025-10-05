#include <queue>

#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"

#include "derivation-check.hh"

namespace nix {

void checkOutputs(
    Store & store,
    const StorePath & drvPath,
    const decltype(Derivation::outputs) & drvOutputs,
    const decltype(DerivationOptions::outputChecks) & outputChecks,
    const std::map<std::string, ValidPathInfo> & outputs)
{
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(store.printStorePath(output.second.path), output.second);

    for (auto & pair : outputs) {
        // We can't use auto destructuring here because
        // clang-tidy seems to complain about it.
        const std::string & outputName = pair.first;
        const auto & info = pair.second;

        auto * outputSpec = get(drvOutputs, outputName);
        assert(outputSpec);

        if (const auto * dof = std::get_if<DerivationOutput::CAFixed>(&outputSpec->raw)) {
            auto & wanted = dof->ca.hash;

            /* Check wanted hash */
            assert(info.ca);
            auto & got = info.ca->hash;
            if (wanted != got) {
                /* Throw an error after registering the path as
                   valid. */
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
        }

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

                auto i = outputsByPath.find(store.printStorePath(path));
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

        auto applyChecks = [&](const DerivationOptions::OutputChecks & checks) {
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

            auto checkRefs = [&](const StringSet & value, bool allowed, bool recursive) {
                /* Parse a list of reference specifiers.  Each element must
                   either be a store path, or the symbolic name of the output
                   of the derivation (such as `out'). */
                StorePathSet spec;
                for (auto & i : value) {
                    if (store.isStorePath(i))
                        spec.insert(store.parseStorePath(i));
                    else if (auto output = get(outputs, i))
                        spec.insert(output->path);
                    else {
                        std::string outputsListing =
                            concatMapStringsSep(", ", outputs, [](auto & o) { return o.first; });
                        throw BuildError(
                            BuildResult::Failure::OutputRejected,
                            "derivation '%s' output check for '%s' contains an illegal reference specifier '%s',"
                            " expected store path or output name (one of [%s])",
                            store.printStorePath(drvPath),
                            outputName,
                            i,
                            outputsListing);
                    }
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

        std::visit(
            overloaded{
                [&](const DerivationOptions::OutputChecks & checks) { applyChecks(checks); },
                [&](const std::map<std::string, DerivationOptions::OutputChecks> & checksPerOutput) {
                    if (auto outputChecks = get(checksPerOutput, outputName))

                        applyChecks(*outputChecks);
                },
            },
            outputChecks);
    }
}

} // namespace nix
