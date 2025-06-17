namespace nix {

struct ExternalDerivationBuilder : DerivationBuilderImpl
{
    Settings::ExternalBuilder externalBuilder;

    ExternalDerivationBuilder(
        Store & store,
        std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        Settings::ExternalBuilder externalBuilder)
        : DerivationBuilderImpl(store, std::move(miscMethods), std::move(params))
        , externalBuilder(std::move(externalBuilder))
    {
    }

    static std::unique_ptr<ExternalDerivationBuilder> newIfSupported(
        Store & store, std::unique_ptr<DerivationBuilderCallbacks> & miscMethods, DerivationBuilderParams & params)
    {
        for (auto & handler : settings.externalBuilders.get()) {
            for (auto & system : handler.systems)
                if (params.drv.platform == system)
                    return std::make_unique<ExternalDerivationBuilder>(
                        store, std::move(miscMethods), std::move(params), handler);
        }
        return {};
    }

    bool prepareBuild() override
    {
        // External builds don't use build users, so this always
        // succeeds.
        return true;
    }

    Path tmpDirInSandbox() override
    {
        /* In a sandbox, for determinism, always use the same temporary
           directory. */
        return "/build";
    }

    void setBuildTmpDir() override
    {
        tmpDir = topTmpDir + "/build";
        createDir(tmpDir, 0700);
    }

    void prepareUser() override
    {
        // Nothing to do here since we don't have a build user.
    }

    void checkSystem() override
    {
        // FIXME: should check system features.
    }

    void startChild() override
    {
        if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
            throw Error("'recursive-nix' is not supported yet by external derivation builders");

        auto json = nlohmann::json::object();

        json.emplace("builder", drv.builder);
        {
            auto l = nlohmann::json::array();
            for (auto & i : drv.args)
                l.push_back(rewriteStrings(i, inputRewrites));
            json.emplace("args", std::move(l));
        }
        {
            auto j = nlohmann::json::object();
            for (auto & [name, value] : env)
                j.emplace(name, rewriteStrings(value, inputRewrites));
            json.emplace("env", std::move(j));
        }
        json.emplace("topTmpDir", topTmpDir);
        json.emplace("tmpDir", tmpDir);
        json.emplace("tmpDirInSandbox", tmpDirInSandbox());
        json.emplace("storeDir", store.storeDir);
        json.emplace("realStoreDir", getLocalStore(store).config->realStoreDir.get());
        json.emplace("system", drv.platform);

        // FIXME: maybe write this JSON into the builder's stdin instead....?
        auto jsonFile = topTmpDir + "/build.json";
        writeFile(jsonFile, json.dump());

        pid = startProcess([&]() {
            openSlave();
            try {
                commonChildInit();

                Strings args = {externalBuilder.program};

                if (externalBuilder.args) {
                    args.insert(args.end(), externalBuilder.args->begin(), externalBuilder.args->end());
                }

                args.insert(args.end(), jsonFile);

                debug("executing external builder: %s", concatStringsSep(" ", args));
                execv(externalBuilder.program.c_str(), stringsToCharPtrs(args).data());

                throw SysError("executing '%s'", externalBuilder.program);
            } catch (...) {
                handleChildException(true);
                _exit(1);
            }
        });
    }
};

}
