#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#include "path.hh"
#include "store-api.hh"
#include "build-result.hh"

#include "globals.hh"

nix_err nix_libstore_init(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::initLibStore();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_plugins(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::initPlugins();
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

        nix::Store::Params params_map;
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

nix_err nix_store_get_uri(nix_c_context * context, Store * store, void * callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto res = store->ptr->getUri();
        return call_nix_get_string_callback(res, callback, user_data);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_store_get_version(nix_c_context * context, Store * store, void * callback, void * user_data)
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

void nix_store_path_free(StorePath * sp)
{
    delete sp;
}
