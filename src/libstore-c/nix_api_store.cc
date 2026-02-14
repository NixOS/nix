#include <cstring>
#include <span>

#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#include "nix/store/path.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/store/store-reference.hh"
#include "nix/store/build-result.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/util/base-nix-32.hh"

#include "nix/store/globals.hh"

extern "C" {

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

        auto storeRef = nix::StoreReference::parse(uri_str);

        if (params) {
            for (size_t i = 0; params[i] != nullptr; i++) {
                storeRef.params[params[i][0]] = params[i][1];
            }
        }
        return new Store{nix::openStore(std::move(storeRef))};
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

bool nix_store_is_valid_path(nix_c_context * context, Store * store, const StorePath * path)
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
        auto store2 = store->ptr.dynamic_pointer_cast<nix::LocalFSStore>();
        auto res = store2 ? store2->toRealPath(path->path) : store->ptr->printStorePath(path->path);
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

nix_err nix_store_get_fs_closure(
    nix_c_context * context,
    Store * store,
    const StorePath * store_path,
    bool flip_direction,
    bool include_outputs,
    bool include_derivers,
    void * userdata,
    void (*callback)(nix_c_context * context, void * userdata, const StorePath * store_path))
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        const auto nixStore = store->ptr;

        nix::StorePathSet set;
        nixStore->computeFSClosure(store_path->path, set, flip_direction, include_outputs, include_derivers);

        if (callback) {
            for (const auto & path : set) {
                const StorePath tmp{path};
                callback(context, userdata, &tmp);
                if (context && context->last_err_code != NIX_OK)
                    return context->last_err_code;
            }
        }
    }
    NIXC_CATCH_ERRS
}

nix_err nix_store_realise(
    nix_c_context * context,
    Store * store,
    StorePath * path,
    void * userdata,
    void (*callback)(void * userdata, const char *, const StorePath *))
{
    if (context)
        context->last_err_code = NIX_OK;
    try {

        const std::vector<nix::DerivedPath> paths{nix::DerivedPath::Built{
            .drvPath = nix::makeConstantStorePathRef(path->path), .outputs = nix::OutputsSpec::All{}}};

        const auto nixStore = store->ptr;
        auto results = nixStore->buildPathsWithResults(paths, nix::bmNormal, nixStore);

        assert(results.size() == 1);

        // Check if any builds failed
        for (auto & result : results)
            result.tryThrowBuildError();

        if (callback) {
            for (const auto & result : results) {
                if (auto * success = result.tryGetSuccess()) {
                    for (const auto & [outputName, realisation] : success->builtOutputs) {
                        StorePath p{realisation.outPath};
                        callback(userdata, outputName.c_str(), &p);
                    }
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

void nix_derivation_free(nix_derivation * drv)
{
    delete drv;
}

StorePath * nix_store_path_clone(const StorePath * p)
{
    try {
        return new StorePath{p->path};
    } catch (...) {
        return nullptr;
    }
}

} // extern "C"

template<size_t S>
static auto to_cpp_array(const uint8_t (&r)[S])
{
    return reinterpret_cast<const std::array<std::byte, S> &>(r);
}

extern "C" {

nix_err
nix_store_path_hash(nix_c_context * context, const StorePath * store_path, nix_store_path_hash_part * hash_part_out)
{
    try {
        auto hashPart = store_path->path.hashPart();
        // Decode from Nix32 (base32) encoding to raw bytes
        auto decoded = nix::BaseNix32::decode(hashPart);

        assert(decoded.size() == sizeof(hash_part_out->bytes));
        std::memcpy(hash_part_out->bytes, decoded.data(), sizeof(hash_part_out->bytes));
        return NIX_OK;
    }
    NIXC_CATCH_ERRS
}

StorePath * nix_store_create_from_parts(
    nix_c_context * context, const nix_store_path_hash_part * hash, const char * name, size_t name_len)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        // Encode the 20 raw bytes to Nix32 (base32) format
        auto hashStr = nix::BaseNix32::encode(std::span<const std::byte>{to_cpp_array(hash->bytes)});

        // Construct the store path basename: <hash>-<name>
        std::string baseName;
        baseName += hashStr;
        baseName += "-";
        baseName += std::string_view{name, name_len};

        return new StorePath{nix::StorePath(std::move(baseName))};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_derivation * nix_derivation_clone(const nix_derivation * d)
{
    try {
        return new nix_derivation{d->drv};
    } catch (...) {
        return nullptr;
    }
}

nix_derivation * nix_derivation_from_json(nix_c_context * context, Store * store, const char * json)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        return new nix_derivation{nix::Derivation::parseJsonAndValidate(*store->ptr, nlohmann::json::parse(json))};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_derivation_to_json(
    nix_c_context * context, const nix_derivation * drv, nix_get_string_callback callback, void * userdata)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto result = static_cast<nlohmann::json>(drv->drv).dump();
        if (callback) {
            callback(result.data(), result.size(), userdata);
        }
    }
    NIXC_CATCH_ERRS
}

StorePath * nix_add_derivation(nix_c_context * context, Store * store, nix_derivation * derivation)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        /* Quite dubious that users would want this to silently suceed
           without actually writing the derivation if this setting is
           set, but it was that way already, so we are doing this for
           back-compat for now. */
        auto ret = nix::settings.readOnlyMode ? nix::computeStorePath(*store->ptr, derivation->drv)
                                              : store->ptr->writeDerivation(derivation->drv, nix::NoRepair);

        return new StorePath{ret};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_store_copy_closure(nix_c_context * context, Store * srcStore, Store * dstStore, StorePath * path)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::StorePathSet paths;
        paths.insert(path->path);
        nix::copyClosure(*srcStore->ptr, *dstStore->ptr, paths);
    }
    NIXC_CATCH_ERRS
}

nix_derivation * nix_store_drv_from_store_path(nix_c_context * context, Store * store, const StorePath * path)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        return new nix_derivation{store->ptr->derivationFromPath(path->path)};
    }
    NIXC_CATCH_ERRS_NULL
}

StorePath * nix_store_query_path_from_hash_part(nix_c_context * context, Store * store, const char * hash)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        std::optional<nix::StorePath> s = store->ptr->queryPathFromHashPart(hash);

        if (!s.has_value()) {
            return nullptr;
        }

        return new StorePath{std::move(s.value())};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_store_copy_path(
    nix_c_context * context, Store * srcStore, Store * dstStore, const StorePath * path, bool repair, bool checkSigs)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        if (srcStore == nullptr)
            return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Source store is null");

        if (dstStore == nullptr)
            return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Destination store is null");

        if (path == nullptr)
            return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Store path is null");

        auto repairFlag = repair ? nix::RepairFlag::Repair : nix::RepairFlag::NoRepair;
        auto checkSigsFlag = checkSigs ? nix::CheckSigsFlag::CheckSigs : nix::CheckSigsFlag::NoCheckSigs;
        nix::copyStorePath(*srcStore->ptr, *dstStore->ptr, path->path, repairFlag, checkSigsFlag);
    }
    NIXC_CATCH_ERRS
}

} // extern "C"
