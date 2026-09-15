#include "nix/store/build/substitution-plan-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/worker-settings.hh"
#include "nix/util/callback.hh"

namespace nix {

SubstitutionPlanGoal::SubstitutionPlanGoal(
    const StorePath & storePath, Worker & worker, std::optional<ContentAddress> ca)
    : Goal(worker, init())
    , storePath(storePath)
    , ca(std::move(ca))
{
    name = fmt("planning substitution of '%s'", worker.store.printStorePath(this->storePath));
    trace("created");
}

SubstitutionPlanGoal::~SubstitutionPlanGoal() {}

asio::awaitable<std::optional<SubstitutionPlanGoal::Candidate>> SubstitutionPlanGoal::findSubstitute(
    Worker & worker,
    std::list<ref<Store>> & subs,
    const StorePath & storePath,
    const std::optional<ContentAddress> & ca)
{
    std::optional<Error> lastStoresException = std::nullopt;

    while (!subs.empty()) {
        auto sub = std::move(subs.front());
        subs.pop_front();

        debug("trying next substituter for '%s'", worker.store.printStorePath(storePath));
        if (lastStoresException.has_value()) {
            logError(lastStoresException->info());
            lastStoresException.reset();
        }

        std::optional<StorePath> subPath;
        std::shared_ptr<const ValidPathInfo> info;

        if (ca) {
            subPath = sub->makeFixedOutputPathFromCA(
                std::string{storePath.name()}, ContentAddressWithReferences::withoutRefs(*ca));
            if (sub->storeDir == worker.store.storeDir)
                assert(subPath == storePath);
        } else if (sub->storeDir != worker.store.storeDir) {
            continue;
        }

        try {
            info = co_await callbackToAwaitable<ref<const ValidPathInfo>>(
                [sub, path = subPath.value_or(storePath)](auto cb) { sub->queryPathInfo(path, std::move(cb)); });
        } catch (InvalidPath &) {
            continue;
        } catch (SubstituterDisabled & e) {
            continue;
        } catch (Error & e) {
            lastStoresException = std::make_optional(std::move(e));
            continue;
        }

        if (info->path != storePath) {
            if (info->isContentAddressed(*sub) && info->references.empty()) {
                auto info2 = std::make_shared<ValidPathInfo>(*info);
                info2->path = storePath;
                info = info2;
            } else {
                printError(
                    "asked '%s' for '%s' but got '%s'",
                    sub->config.getHumanReadableURI(),
                    worker.store.printStorePath(storePath),
                    sub->printStorePath(info->path));
                continue;
            }
        }

        /* Bail out early if this substituter lacks a valid
           signature. LocalStore::addToStore() also checks for this, but
           only after we've downloaded the path. */
        if (!sub->config.isTrusted && worker.store.pathInfoIsUntrusted(*info)) {
            warn(
                "ignoring substitute for '%s' from '%s', as it's not signed by any of the keys in 'trusted-public-keys'",
                worker.store.printStorePath(storePath),
                sub->config.getHumanReadableURI());
            continue;
        }

        co_return Candidate{
            .sub = sub,
            .subPath = subPath.value_or(storePath),
            .info = std::move(info),
        };
    }

    if (lastStoresException.has_value()) {
        if (!worker.settings.tryFallback) {
            throw std::move(*lastStoresException);
        } else
            logError(lastStoresException->info());
    }

    co_return std::nullopt;
}

asio::awaitable<Goal::ExitCode> SubstitutionPlanGoal::init()
{
    trace("init");

    /* If the path already exists there is nothing to plan. */
    if (worker.store.isValidPath(storePath))
        co_return ecSuccess;

    auto subs = worker.getSubstituters();
    candidate = co_await findSubstitute(worker, subs, storePath, ca);

    if (!candidate) {
        debug("no substituter can provide '%s'", worker.store.printStorePath(storePath));
        co_return ecNoSubstituters;
    }

    /* The plan for a path covers its closure. */
    for (auto & i : candidate->info->references)
        if (i != storePath) /* ignore self-references */
            referencePlans.insert(worker.makeSubstitutionPlanGoal(i));

    co_await await(referencePlans);

    trace("planned");

    co_return ecSuccess;
}

} // namespace nix
