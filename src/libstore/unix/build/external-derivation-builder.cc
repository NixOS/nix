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
        experimentalFeatureSettings.require(Xp::ExternalBuilders);
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
        return DerivationBuilderImpl::prepareBuild();
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
        DerivationBuilderImpl::prepareUser();
    }

    void setUser() override
    {
        DerivationBuilderImpl::setUser();
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

        // TODO(cole-h): writing this to stdin is too much effort right now, if we want to revisit
        // that, see this comment by Eelco about how to make it not suck:
        // https://github.com/DeterminateSystems/nix-src/pull/141#discussion_r2205493257
        auto jsonFile = std::filesystem::path{topTmpDir} / "build.json";
        writeFile(jsonFile, json.dump());

        pid = startProcess([&]() {
            openSlave();
            try {
                commonChildInit();

                Strings args = {externalBuilder.program};

                if (!externalBuilder.args.empty()) {
                    args.insert(args.end(), externalBuilder.args.begin(), externalBuilder.args.end());
                }

                args.insert(args.end(), jsonFile);

                if (chdir(tmpDir.c_str()) == -1)
                    throw SysError("changing into '%1%'", tmpDir);

                chownToBuilder(topTmpDir);

                setUser();

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
