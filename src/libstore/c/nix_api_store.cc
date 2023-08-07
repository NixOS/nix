#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#include "store-api.hh"

#include "globals.hh"

struct StorePath {
  nix::StorePath path;
};

nix_err nix_libstore_init(nix_c_context *context) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    nix::initLibStore();
  }
  NIXC_CATCH_ERRS
}

nix_err nix_init_plugins(nix_c_context *context) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    nix::initPlugins();
  }
  NIXC_CATCH_ERRS
}

Store *nix_store_open(nix_c_context *context, const char *uri,
                      const char ***params) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    if (!uri) {
      return new Store{nix::openStore()};
    } else {
      std::string uri_str = uri;
      if (!params)
        return new Store{nix::openStore(uri_str)};

      nix::Store::Params params_map;
      for (size_t i = 0; params[i] != nullptr; i++) {
        params_map[params[i][0]] = params[i][1];
      }
      return new Store{nix::openStore(uri_str, params_map)};
    }
  }
  NIXC_CATCH_ERRS_NULL
}

void nix_store_unref(Store *store) { delete store; }

nix_err nix_store_get_uri(nix_c_context *context, Store *store, char *dest,
                          unsigned int n) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    auto res = store->ptr->getUri();
    return nix_export_std_string(context, res, dest, n);
  }
  NIXC_CATCH_ERRS
}

nix_err nix_store_get_version(nix_c_context *context, Store *store, char *dest,
                              unsigned int n) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    auto res = store->ptr->getVersion();
    if (res) {
      return nix_export_std_string(context, *res, dest, n);
    } else {
      return nix_set_err_msg(context, NIX_ERR_UNKNOWN,
                             "store does not have a version");
    }
  }
  NIXC_CATCH_ERRS
}

bool nix_store_is_valid_path(nix_c_context *context, Store *store,
                             StorePath *path) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    return store->ptr->isValidPath(path->path);
  }
  NIXC_CATCH_ERRS_RES(false);
}

StorePath *nix_store_parse_path(nix_c_context *context, Store *store,
                                const char *path) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    nix::StorePath s = store->ptr->parseStorePath(path);
    return new StorePath{std::move(s)};
  }
  NIXC_CATCH_ERRS_NULL
}

nix_err nix_store_build(nix_c_context *context, Store *store, StorePath *path,
                        void *userdata,
                        void (*iter)(void *userdata, const char *,
                                     const char *)) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    store->ptr->buildPaths({
        nix::DerivedPath::Built{
            .drvPath = path->path,
            .outputs = nix::OutputsSpec::All{},
        },
    });
    if (iter) {
      for (auto &[outputName, outputPath] :
           store->ptr->queryDerivationOutputMap(path->path)) {
        auto op = store->ptr->printStorePath(outputPath);
        iter(userdata, outputName.c_str(), op.c_str());
      }
    }
  }
  NIXC_CATCH_ERRS
}

void nix_store_path_free(StorePath *sp) { delete sp; }
