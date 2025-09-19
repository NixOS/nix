#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#include "nix/store/path.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/store/build-result.hh"

#include "nix/store/globals.hh"

nix_err nix_libstore_init(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::initLibStore();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_libstore_init_no_load_config(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::initLibStore(false);
    }
    NIXC_CATCH_ERRS
}

Store * nix_store_open(nix_c_context * context, const char * uri, const char *** params)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        std::string uri_str = uri ? uri : "";

        if (uri_str.empty())
            return new Store{nix::openStore()};

        if (!params)
            return new Store{nix::openStore(uri_str)};

        nix::Store::Config::Params params_map;
        for (size_t i = 0; params[i] != nullptr; i++) {
            params_map[params[i][0]] = params[i][1];
        }
        return new Store{nix::openStore(uri_str, params_map)};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_store_free(Store * store)
{
    delete store;
}

nix_err nix_store_get_uri(nix_c_context * context, Store * store, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto res = store->ptr->config.getReference().render(/*withParams=*/true);
        return call_nix_get_string_callback(res, callback, user_data);
    }
    NIXC_CATCH_ERRS
}

nix_err
nix_store_get_storedir(nix_c_context * context, Store * store, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        return call_nix_get_string_callback(store->ptr->storeDir, callback, user_data);
    }
    NIXC_CATCH_ERRS
}

nix_err
nix_store_get_version(nix_c_context * context, Store * store, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto res = store->ptr->getVersion();
        return call_nix_get_string_callback(res.value_or(""), callback, user_data);
    }
    NIXC_CATCH_ERRS
}

bool nix_store_is_valid_path(nix_c_context * context, Store * store, StorePath * path)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        return store->ptr->isValidPath(path->path);
    }
    NIXC_CATCH_ERRS_RES(false);
}

nix_err nix_store_real_path(
    nix_c_context * context, Store * store, StorePath * path, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto res = store->ptr->toRealPath(path->path);
        return call_nix_get_string_callback(res, callback, user_data);
    }
    NIXC_CATCH_ERRS
}

StorePath * nix_store_parse_path(nix_c_context * context, Store * store, const char * path)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::StorePath s = store->ptr->parseStorePath(path);
        return new StorePath{std::move(s)};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_store_realise(
    nix_c_context * context,
    Store * store,
    StorePath * path,
    void * userdata,
    void (*callback)(void * userdata, const char *, const char *))
{
    if (context)
        context->last_err_code = NIX_OK;
    try {

        const std::vector<nix::DerivedPath> paths{nix::DerivedPath::Built{
            .drvPath = nix::makeConstantStorePathRef(path->path), .outputs = nix::OutputsSpec::All{}}};

        const auto nixStore = store->ptr;
        auto results = nixStore->buildPathsWithResults(paths, nix::bmNormal, nixStore);

        if (callback) {
            for (const auto & result : results) {
                for (const auto & [outputName, realisation] : result.builtOutputs) {
                    auto op = store->ptr->printStorePath(realisation.outPath);
                    callback(userdata, outputName.c_str(), op.c_str());
                }
            }
        }
    }
    NIXC_CATCH_ERRS
}

void nix_store_path_name(const StorePath * store_path, nix_get_string_callback callback, void * user_data)
{
    std::string_view name = store_path->path.name();
    callback(name.data(), name.size(), user_data);
}

void nix_store_path_free(StorePath * sp)
{
    delete sp;
}

StorePath * nix_store_path_clone(const StorePath * p)
{
    return new StorePath{p->path};
}

nix_err nix_store_copy_closure(nix_c_context * context, Store * srcStore, Store * dstStore, StorePath * path)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::RealisedPath::Set paths;
        paths.insert(path->path);
        nix::copyClosure(*srcStore->ptr, *dstStore->ptr, paths);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_store_drv_from_path(
    nix_c_context * context,
    Store * store,
    const StorePath * path,
    void (*callback)(void * userdata, const Derivation * drv),
    void * userdata)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::Derivation drv = store->ptr->derivationFromPath(path->path);
        if (callback) {
            const Derivation tmp{drv};
            callback(userdata, &tmp);
        }
    }
    NIXC_CATCH_ERRS
}

Derivation * nix_drv_clone(const Derivation * d)
{
    return new Derivation{d->drv};
}

void nix_drv_free(Derivation * d)
{
    delete d;
}

nix_err nix_drv_get_outputs(
    nix_c_context * context,
    const Derivation * drv,
    void (*callback)(void * userdata, const char * name, const DerivationOutput * drv_output),
    void * userdata)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        if (callback) {
            for (const auto & [name, result] : drv->drv.outputs) {
                const DerivationOutput tmp{result};
                callback(userdata, name.c_str(), &tmp);
            }
        }
    }
    NIXC_CATCH_ERRS
}

nix_err nix_drv_get_outputs_and_optpaths(
    nix_c_context * context,
    const Derivation * drv,
    const Store * store,
    void (*callback)(void * userdata, const char * name, const DerivationOutput * drv_output, const StorePath * path),
    void * userdata)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto value = drv->drv.outputsAndOptPaths(store->ptr->config);
        if (callback) {
            for (const auto & [name, result] : value) {
                const DerivationOutput tmp_output{result.first};

                if (auto store_path = result.second) {
                    const StorePath tmp_path{*store_path};
                    callback(userdata, name.c_str(), &tmp_output, &tmp_path);
                } else {
                    callback(userdata, name.c_str(), &tmp_output, nullptr);
                }
            }
        }
    }
    NIXC_CATCH_ERRS
}

DerivationOutput * nix_drv_output_clone(const DerivationOutput * o)
{
    return new DerivationOutput{o->drv_out};
}

void nix_drv_output_free(DerivationOutput * o)
{
    delete o;
}
