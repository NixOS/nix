#include "command.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "common-args.hh"
#include "json.hh"
#include "get-drvs.hh"
#include "attr-path.hh"

#include <nlohmann/json.hpp>
#include <sys/resource.h>

using namespace nix;

static std::string queryMetaStrings(EvalState & state, DrvInfo & drv, const string & name, const string & subAttribute)
{
    Strings res;
    std::function<void(Value & v)> rec;

    rec = [&](Value & v) {
        state.forceValue(v);
        if (v.type == tString)
            res.push_back(v.string.s);
        else if (v.isList())
            for (unsigned int n = 0; n < v.listSize(); ++n)
                rec(*v.listElems()[n]);
        else if (v.type == tAttrs) {
            auto a = v.attrs->find(state.symbols.create(subAttribute));
            if (a != v.attrs->end())
                res.push_back(state.forceString(*a->value));
        }
    };

    Value * v = drv.queryMeta(name);
    if (v) rec(*v);

    return concatStringsSep(", ", res);
}

struct CmdEvalHydraJobs : MixJSON, MixDryRun, InstallableCommand
{
    std::optional<Path> gcRootsDir;
    size_t nrWorkers = 1;
    size_t maxMemorySize = 4ULL * 1024;

    CmdEvalHydraJobs()
    {
        mkFlag()
            .longName("gc-roots-dir")
            .description("garbage collector roots directory")
            .labels({"path"})
            .dest(&gcRootsDir);

        mkIntFlag(0, "workers", "number of concurrent worker processes", &nrWorkers);

        mkIntFlag(0, "max-memory-size", "maximum memory usage per worker process (in MiB)", &maxMemorySize);
    }

    std::string description() override
    {
        return "evaluate a Hydra jobset";
    }

    Examples examples() override
    {
        return {
            Example{
                "Evaluate Nixpkgs' release-combined jobset:",
                "nix eval-hydra-jobs -f '<nixpkgs/nixos/release-combined.nix>' '' --json"
            },
        };
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {"hydraJobs", "checks"};
    }

    void worker(AutoCloseFD & to, AutoCloseFD & from)
    {
        auto state = getEvalState();

        // FIXME: should re-open state->store.

        if (dryRun) settings.readOnlyMode = true;

        /* Prevent access to paths outside of the Nix search path and
           to the environment. */
        evalSettings.restrictEval = true;

        auto autoArgs = getAutoArgs(*state);

        auto vTop = installable->toValue(*state).first;

        auto vRoot = state->allocValue();
        state->autoCallFunction(*autoArgs, *vTop, *vRoot);

        while (true) {
            /* Wait for the master to send us a job name. */
            writeLine(to.get(), "next");

            auto s = readLine(from.get());
            if (s == "exit") break;
            if (!hasPrefix(s, "do ")) abort();
            std::string attrPath(s, 3);

            debug("worker process %d at '%s'", getpid(), attrPath);

            /* Evaluate it and send info back to the master. */
            nlohmann::json reply;

            try {
                auto v = findAlongAttrPath(*state, attrPath, *autoArgs, *vRoot).first;

                state->forceValue(*v);

                if (auto drv = getDerivation(*state, *v, false)) {

                    DrvInfo::Outputs outputs = drv->queryOutputs();

                    if (drv->querySystem() == "unknown")
                        throw EvalError("derivation must have a 'system' attribute");

                    auto drvPath = drv->queryDrvPath();

                    nlohmann::json job;

                    job["nixName"] = drv->queryName();
                    job["system"] =drv->querySystem();
                    job["drvPath"] = drvPath;
                    job["description"] = drv->queryMetaString("description");
                    job["license"] = queryMetaStrings(*state, *drv, "license", "shortName");
                    job["homepage"] = drv->queryMetaString("homepage");
                    job["maintainers"] = queryMetaStrings(*state, *drv, "maintainers", "email");
                    job["schedulingPriority"] = drv->queryMetaInt("schedulingPriority", 100);
                    job["timeout"] = drv->queryMetaInt("timeout", 36000);
                    job["maxSilent"] = drv->queryMetaInt("maxSilent", 7200);
                    job["isChannel"] = drv->queryMetaBool("isHydraChannel", false);

                    /* If this is an aggregate, then get its constituents. */
                    auto a = v->attrs->get(state->symbols.create("_hydraAggregate"));
                    if (a && state->forceBool(*a->value, *a->pos)) {
                        auto a = v->attrs->get(state->symbols.create("constituents"));
                        if (!a)
                            throw EvalError("derivation must have a ‘constituents’ attribute");


                        PathSet context;
                        state->coerceToString(*a->pos, *a->value, context, true, false);
                        for (auto & i : context)
                            if (i.at(0) == '!') {
                                size_t index = i.find("!", 1);
                                job["constituents"].push_back(string(i, index + 1));
                            }

                        state->forceList(*a->value, *a->pos);
                        for (unsigned int n = 0; n < a->value->listSize(); ++n) {
                            auto v = a->value->listElems()[n];
                            state->forceValue(*v);
                            if (v->type == tString)
                                job["namedConstituents"].push_back(state->forceStringNoCtx(*v));
                        }
                    }

                    /* Register the derivation as a GC root.  !!! This
                       registers roots for jobs that we may have already
                       done. */
                    auto localStore = state->store.dynamic_pointer_cast<LocalFSStore>();
                    if (gcRootsDir && localStore) {
                        Path root = *gcRootsDir + "/" + std::string(baseNameOf(drvPath));
                        if (!pathExists(root))
                            localStore->addPermRoot(localStore->parseStorePath(drvPath), root, false);
                    }

                    nlohmann::json out;
                    for (auto & j : outputs)
                        out[j.first] = j.second;
                    job["outputs"] = std::move(out);

                    reply["job"] = std::move(job);
                }

                else if (v->type == tAttrs) {
                    auto attrs = nlohmann::json::array();
                    StringSet ss;
                    for (auto & i : v->attrs->lexicographicOrder()) {
                        std::string name(i->name);
                        if (name.find('.') != std::string::npos || name.find(' ') != std::string::npos) {
                            printError("skipping job with illegal name '%s'", name);
                            continue;
                        }
                        attrs.push_back(name);
                    }
                    reply["attrs"] = std::move(attrs);
                }

            } catch (EvalError & e) {
                reply["error"] = filterANSIEscapes(e.msg(), true);
            }

            writeLine(to.get(), reply.dump());

            /* If our RSS exceeds the maximum, exit. The master will
               start a new process. */
            struct rusage r;
            getrusage(RUSAGE_SELF, &r);
            if ((size_t) r.ru_maxrss > maxMemorySize * 1024) break;
        }

        writeLine(to.get(), "restart");
    }

    void run(ref<Store> store) override
    {
        if (!gcRootsDir) warn("'--gc-roots-dir' not specified");

        struct State
        {
            std::set<std::string> todo{""};
            std::set<std::string> active;
            nlohmann::json jobs;
            std::exception_ptr exc;
        };

        std::condition_variable wakeup;

        Sync<State> state_;

        /* Start a handler thread per worker process. */
        auto handler = [this, &state_, &wakeup]()
        {
            try {
                pid_t pid = -1;
                AutoCloseFD from, to;

                while (true) {

                    /* Start a new worker process if necessary. */
                    if (pid == -1) {
                        Pipe toPipe, fromPipe;
                        toPipe.create();
                        fromPipe.create();
                        pid = startProcess(
                            [this,
                             to{std::make_shared<AutoCloseFD>(std::move(fromPipe.writeSide))},
                             from{std::make_shared<AutoCloseFD>(std::move(toPipe.readSide))}
                            ]()
                            {
                                try {
                                    worker(*to, *from);
                                } catch (std::exception & e) {
                                    nlohmann::json err;
                                    err["error"] = e.what();
                                    writeLine(to->get(), err.dump());
                                }
                            },
                            ProcessOptions { .allowVfork = false });
                        from = std::move(fromPipe.readSide);
                        to = std::move(toPipe.writeSide);
                        debug("created worker process %d", pid);
                    }

                    /* Check whether the existing worker process is still there. */
                    auto s = readLine(from.get());
                    if (s == "restart") {
                        pid = -1;
                        continue;
                    } else if (s != "next") {
                        auto json = nlohmann::json::parse(s);
                        throw Error("worker error: %s", (std::string) json["error"]);
                    }

                    /* Wait for a job name to become available. */
                    std::string attrPath;

                    while (true) {
                        checkInterrupt();
                        auto state(state_.lock());
                        if ((state->todo.empty() && state->active.empty()) || state->exc) {
                            writeLine(to.get(), "exit");
                            return;
                        }
                        if (!state->todo.empty()) {
                            attrPath = *state->todo.begin();
                            state->todo.erase(state->todo.begin());
                            state->active.insert(attrPath);
                            break;
                        } else
                            state.wait(wakeup);
                    }

                    Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", attrPath));

                    /* Tell the worker to evaluate it. */
                    writeLine(to.get(), "do " + attrPath);

                    /* Wait for the response. */
                    auto response = nlohmann::json::parse(readLine(from.get()));

                    /* Handle the response. */
                    StringSet newAttrs;

                    if (response.find("job") != response.end()) {
                        auto state(state_.lock());
                        if (json)
                            state->jobs[attrPath] = response["job"];
                        else
                            std::cout << fmt("%d: %d\n", attrPath, (std::string) response["job"]["drvPath"]);
                    }

                    if (response.find("attrs") != response.end()) {
                        for (auto & i : response["attrs"]) {
                            auto s = (attrPath.empty() ? "" : attrPath + ".") + (std::string) i;
                            newAttrs.insert(s);
                        }
                    }

                    if (response.find("error") != response.end()) {
                        auto state(state_.lock());
                        if (json)
                            state->jobs[attrPath]["error"] = response["error"];
                        else
                            printError("error in job '%s': %s",
                                attrPath, (std::string) response["error"]);
                    }

                    /* Add newly discovered job names to the queue. */
                    {
                        auto state(state_.lock());
                        state->active.erase(attrPath);
                        for (auto & s : newAttrs)
                            state->todo.insert(s);
                        wakeup.notify_all();
                    }
                }
            } catch (...) {
                auto state(state_.lock());
                state->exc = std::current_exception();
                wakeup.notify_all();
            }
        };

        std::vector<std::thread> threads;
        for (size_t i = 0; i < nrWorkers; i++)
            threads.emplace_back(std::thread(handler));

        for (auto & thread : threads)
            thread.join();

        auto state(state_.lock());

        if (state->exc)
            std::rethrow_exception(state->exc);

        /* For aggregate jobs that have named consistuents
           (i.e. constituents that are a job name rather than a
           derivation), look up the referenced job and add it to the
           dependencies of the aggregate derivation. */
        for (auto i = state->jobs.begin(); i != state->jobs.end(); ++i) {
            auto jobName = i.key();
            auto & job = i.value();

            auto named = job.find("namedConstituents");
            if (named == job.end()) continue;

            if (dryRun) {
                for (std::string jobName2 : *named) {
                    auto job2 = state->jobs.find(jobName2);
                    if (job2 == state->jobs.end())
                        throw Error("aggregate job '%s' references non-existent job '%s'", jobName, jobName2);
                    std::string drvPath2 = (*job2)["drvPath"];
                    job["constituents"].push_back(drvPath2);
                }
            } else {
                std::string drvPath = job["drvPath"];
                auto drv = readDerivation(*store, drvPath);

                for (std::string jobName2 : *named) {
                    auto job2 = state->jobs.find(jobName2);
                    if (job2 == state->jobs.end())
                        throw Error("aggregate job '%s' references non-existent job '%s'", jobName, jobName2);
                    std::string drvPath2 = (*job2)["drvPath"];
                    auto drv2 = readDerivation(*store, drvPath2);
                    job["constituents"].push_back(drvPath2);
                    drv.inputDrvs[store->parseStorePath(drvPath2)] = {drv2.outputs.begin()->first};
                }

                std::string drvName(store->parseStorePath(drvPath).name());
                assert(hasSuffix(drvName, drvExtension));
                drvName.resize(drvName.size() - drvExtension.size());
                auto h = hashDerivationModulo(*store, drv, true);
                auto outPath = store->makeOutputPath("out", h, drvName);
                drv.env["out"] = store->printStorePath(outPath);
                drv.outputs.insert_or_assign("out", DerivationOutput(outPath.clone(), "", ""));
                auto newDrvPath = store->printStorePath(writeDerivation(store, drv, drvName));

                debug("rewrote aggregate derivation %s -> %s", drvPath, newDrvPath);

                job["drvPath"] = newDrvPath;
                job["outputs"]["out"] = store->printStorePath(outPath);
            }

            job.erase("namedConstituents");
        }

        if (json) std::cout << state->jobs.dump(2) << "\n";
    }
};

static auto r1 = registerCommand<CmdEvalHydraJobs>("eval-hydra-jobs");
