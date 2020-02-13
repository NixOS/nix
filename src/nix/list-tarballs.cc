#include "command.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "common-args.hh"
#include "json.hh"

using namespace nix;

struct CmdListTarballs : MixJSON, InstallablesCommand
{
    std::string description() override
    {
        return "list the 'fetchurl' calls made by the dependency graph of a package";
    }

    Examples examples() override
    {
        return {
            Example{
                "To get the tarballs required to build GNU Hello and its dependencies:",
                "nix list-tarballs nixpkgs.hello"
            },
        };
    }

    struct File
    {
        std::string type;
        bool recursive;
        Hash hash;
        std::string url;
        StorePath storePath;
    };

    void doIt(ref<Store> store, std::function<void(const File &)> callback)
    {
        settings.readOnlyMode = true;

        auto state = getEvalState();
        auto autoArgs = getAutoArgs(*state);

        StorePathSet done;

        state->derivationHook =
            [&](const StorePath & drvPath, const Derivation & drv) {
                if (drv.outputs.size() != 1) return;

                auto & output = *drv.outputs.begin();

                if (output.second.hashAlgo.empty() || output.second.hash.empty()) return;

                if (!done.insert(output.second.path.clone()).second) return;

                auto [recursive, hash] = output.second.parseHashInfo();

                if (recursive) return; // FIXME

                std::optional<std::string> url;

                auto i = drv.env.find("url");
                if (i != drv.env.end())
                    url = i->second;
                else {
                    i = drv.env.find("urls");
                    if (i == drv.env.end()) return;
                    auto urls = tokenizeString<std::vector<std::string>>(i->second, " ");
                    if (urls.empty()) return;
                    url = urls[0];
                }

                File file {
                    .type = drv.builder == "builtin:fetchurl" ? "fetchurl" : "unknown",
                    .recursive = recursive,
                    .hash = hash,
                    .url = *url,
                    .storePath = output.second.path.clone()
                };

                callback(file);
            };

        std::function<void(Value * v)> findDerivations;

        findDerivations =
            [&](Value * v) {
                state->forceValue(*v);
                if (v->type == tAttrs) {
                    if (state->isDerivation(*v)) {
                        auto aDrvPath = v->attrs->get(state->sDrvPath);
                        if (!aDrvPath) return;
                        try {
                            state->forceValue(*(*aDrvPath)->value, *(*aDrvPath)->pos);
                        } catch (EvalError & e) {
                        }
                    } else {
                        std::unordered_set<Value *> vs;
                        for (auto & attr : *v->attrs) {
                            if (!vs.insert(attr.value).second) continue;
                            findDerivations(attr.value);
                        }
                    }
                }
            };

        for (auto & installable : installables) {
            auto v = state->allocValue();
            state->autoCallFunction(autoArgs, installable->toValue(*state), v);
            findDerivations(v);
        }
    }

    void run(ref<Store> store) override
    {
        if (json) {
            JSONList json(std::cout);
            doIt(store,
                [&](const File & file) {
                    auto obj = json.object();
                    obj.attr("type", file.type);
                    if (file.recursive)
                        obj.attr("recursive", true);
                    obj.attr("hash", file.hash.to_string(SRI));
                    obj.attr("url", file.url);
                    obj.attr("storePath", store->printStorePath(file.storePath));
                });
        } else {
            doIt(store,
                [&](const File & file) {
                    std::cout << file.url << "\n";
                });
        }
    }
};

static auto r1 = registerCommand<CmdListTarballs>("list-tarballs");
