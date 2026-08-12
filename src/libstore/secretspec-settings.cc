#include "nix/store/secretspec-settings.hh"

#include "nix/util/config-global.hh"
#include "nix/util/error.hh"
#include "nix/util/file-system.hh"
#include "nix/util/strings.hh"

#include <memory>
#include <set>

#include <nlohmann/json.hpp>

#include "store-config-private.hh"

#if NIX_WITH_SECRETSPEC
#  include <secretspec.h>
#endif

namespace nix {

std::string SecretSpecSettings::defaultFile()
{
    return NIX_SECRETSPEC_FILE;
}

/* A resolve response carries secret values, so this file deliberately does not
   use the accessors from `json-utils.hh`: those quote the offending JSON in
   their error messages, which would put secrets into diagnostics and logs. */

namespace {

struct ResolvedSecret
{
    bool asPath;
    std::string value;
};

#if !NIX_WITH_SECRETSPEC

std::string resolveWithSecretSpec(const std::string &)
{
    throw Error("Nix was built without SecretSpec support, so it cannot resolve SecretSpec credentials");
}

#else

/** Parse the `major.minor` prefix of a semver ABI version. */
std::pair<unsigned int, unsigned int> parseAbiVersion(const std::string & version)
{
    auto components = tokenizeString<std::vector<std::string>>(version, ".");
    if (components.size() < 2)
        throw Error("cannot parse secretspec-ffi ABI version '%s'", version);
    try {
        return {std::stoul(components[0]), std::stoul(components[1])};
    } catch (std::exception &) {
        throw Error("cannot parse secretspec-ffi ABI version '%s'", version);
    }
}

void checkAbiVersion()
{
    /* The returned pointer is owned by the library and must not be freed. */
    auto version = secretspec_abi_version();
    if (!version || version[0] == '\0')
        throw Error("secretspec-ffi returned an empty ABI version");

    auto [runtimeMajor, runtimeMinor] = parseAbiVersion(version);
    auto [buildMajor, buildMinor] = parseAbiVersion(SECRETSPEC_FFI_VERSION);

    /* Semver: before 1.0 every minor release may break compatibility, after it
       only majors do. */
    bool compatible =
        runtimeMajor == buildMajor && (buildMajor == 0 ? runtimeMinor == buildMinor : runtimeMinor >= buildMinor);

    if (!compatible)
        throw Error(
            "secretspec-ffi reports ABI version '%s', which is incompatible with version '%s' that Nix was built against",
            version,
            SECRETSPEC_FFI_VERSION);
}

std::string resolveWithSecretSpec(const std::string & request)
{
    static const bool checkedAbiVersion = [] {
        checkAbiVersion();
        return true;
    }();
    (void) checkedAbiVersion;

    auto response = secretspec_resolve(request.c_str());
    if (!response)
        throw Error("secretspec-ffi returned a null response while resolving Nix credentials");
    std::unique_ptr<char, decltype(&secretspec_free)> ownedResponse(response, &secretspec_free);
    return ownedResponse.get();
}

#endif

std::string getString(const nlohmann::json & object, const char * field, const std::string & fallback = "")
{
    if (auto i = object.find(field); i != object.end() && i->is_string())
        return i->get<std::string>();
    return fallback;
}

} // namespace

struct SecretSpecCache
{
    std::map<std::string, ResolvedSecret> secrets;
    /** Per-secret validation failures, reported only when that secret is requested. */
    std::map<std::string, std::exception_ptr> errors;
    std::vector<AutoDelete> pathOwners;
};

SecretSpecSettings secretSpecSettings;

static GlobalConfig::Register rSecretSpecSettings(&secretSpecSettings);

SecretSpecSettings::SecretSpecSettings() {}

SecretSpecSettings::~SecretSpecSettings() = default;

static std::shared_ptr<SecretSpecCache> resolveSecrets(const SecretSpecRequest & request)
{
    nlohmann::json requestJson{
        {"mode", "resolve"},
        {"no_values", false},
        {"reason", "Nix credential resolution"},
    };
    if (!request.file.empty())
        requestJson["path"] = request.file;
    if (!request.provider.empty())
        requestJson["provider"] = request.provider;
    if (!request.profile.empty())
        requestJson["profile"] = request.profile;
    if (!request.scope.empty())
        requestJson["scope"] = request.scope;

    auto response = resolveWithSecretSpec(requestJson.dump());
    nlohmann::json envelope;
    try {
        envelope = nlohmann::json::parse(response);
    } catch (const nlohmann::json::exception &) {
        throw Error("secretspec-ffi returned invalid JSON while resolving Nix credentials");
    }

    if (!envelope.is_object() || !envelope.contains("ok") || !envelope["ok"].is_boolean())
        throw Error("secretspec-ffi returned an invalid response envelope while resolving Nix credentials");

    if (!envelope["ok"].get<bool>()) {
        auto error = envelope.find("error");
        auto kind = error != envelope.end() && error->is_object() ? getString(*error, "kind", "unknown") : "unknown";
        auto message = error != envelope.end() && error->is_object() ? getString(*error, "message", "unknown error")
                                                                     : "unknown error";
        throw Error("SecretSpec failed to resolve Nix credentials (kind: %s): %s", kind, message);
    }

    auto responseObject = envelope.find("response");
    if (responseObject == envelope.end() || !responseObject->is_object())
        throw Error("secretspec-ffi returned no resolve response while resolving Nix credentials");

    auto schemaVersion = responseObject->find("schema_version");
    if (schemaVersion == responseObject->end() || !schemaVersion->is_number_unsigned()
        || schemaVersion->get<unsigned int>() != 2)
        throw Error("secretspec-ffi returned an unsupported resolve response schema");

    auto missingRequired = responseObject->find("missing_required");
    if (missingRequired == responseObject->end() || !missingRequired->is_array())
        throw Error("secretspec-ffi returned an invalid missing-required list while resolving Nix credentials");
    if (!missingRequired->empty()) {
        StringSet names;
        for (const auto & item : *missingRequired) {
            if (!item.is_string())
                throw Error("secretspec-ffi returned an invalid missing-required list while resolving Nix credentials");
            names.insert(item.get<std::string>());
        }
        throw Error(
            "SecretSpec is missing required secrets while resolving Nix credentials: %s",
            concatStringsSep(", ", names));
    }

    auto secrets = responseObject->find("secrets");
    if (secrets == responseObject->end() || !secrets->is_object())
        throw Error("secretspec-ffi returned no secrets object while resolving Nix credentials");

    auto result = std::make_shared<SecretSpecCache>();

    /* A response covers every secret in scope, most of which the caller did not
       ask for, so a malformed entry is recorded rather than thrown: it must not
       take down the credentials that did resolve. */
    for (const auto & [name, secret] : secrets->items()) {
        try {
            if (!secret.is_object())
                throw Error("secretspec-ffi returned an invalid value for secret '%s'", name);

            auto asPath = secret.find("as_path");
            if (asPath == secret.end() || !asPath->is_boolean())
                throw Error("secretspec-ffi returned an invalid value for secret '%s'", name);

            if (asPath->get<bool>()) {
                auto path = secret.find("path");
                if (path == secret.end() || !path->is_string() || path->get_ref<const std::string &>().empty())
                    throw Error("SecretSpec path secret '%s' did not contain a path", name);
                AbsolutePath absolutePath{path->get<std::string>()};
                result->secrets.emplace(name, ResolvedSecret{.asPath = true, .value = absolutePath.string()});
                result->pathOwners.emplace_back(absolutePath, /* recursive = */ false);
            } else {
                auto value = secret.find("value");
                if (value == secret.end() || !value->is_string())
                    throw Error("SecretSpec inline secret '%s' did not contain a value", name);
                result->secrets.emplace(name, ResolvedSecret{.asPath = false, .value = value->get<std::string>()});
            }
        } catch (Error &) {
            result->errors.emplace(name, std::current_exception());
        }
    }

    return result;
}

std::shared_ptr<SecretSpecCache> SecretSpecSettings::getResolvedSecrets() const
{
    SecretSpecRequest request{
        .file = file.get(),
        .provider = provider.get(),
        .profile = profile.get(),
        .scope = scope.get(),
    };

    auto lookup = [&]() -> std::shared_ptr<SecretSpecCache> {
        auto caches(_caches.lock());
        if (auto cached = caches->find(request); cached != caches->end())
            return cached->second;
        return nullptr;
    };

    if (auto cached = lookup())
        return cached;

    /* `secretspec_resolve()` blocks for as long as the provider takes, which
       for an interactive provider means until the user answers a prompt. Hold
       a separate lock across it so that cache hits for already-resolved
       requests are never blocked by an in-flight resolution, while still
       resolving each request only once. */
    std::lock_guard resolveLock(_resolveMutex);

    /* Another thread may have resolved this request while we waited. */
    if (auto cached = lookup())
        return cached;

    auto resolved = resolveSecrets(request);
    _caches.lock()->emplace(request, resolved);
    return resolved;
}

static const ResolvedSecret & getSecret(const SecretSpecCache & cache, const std::string & name)
{
    if (auto error = cache.errors.find(name); error != cache.errors.end())
        std::rethrow_exception(error->second);
    auto secret = cache.secrets.find(name);
    if (secret == cache.secrets.end())
        throw Error("SecretSpec did not resolve configured secret '%s'", name);
    return secret->second;
}

std::string SecretSpecSettings::getInlineSecret(const std::string & name) const
{
    auto resolved = getResolvedSecrets();
    auto & secret = getSecret(*resolved, name);
    if (secret.asPath)
        throw Error("SecretSpec secret '%s' must not use 'as_path'", name);
    if (secret.value.empty())
        throw Error("SecretSpec secret '%s' was empty", name);
    return secret.value;
}

AbsolutePath SecretSpecSettings::getPathSecret(const std::string & name) const
{
    auto resolved = getResolvedSecrets();
    auto & secret = getSecret(*resolved, name);
    if (!secret.asPath)
        throw Error("SecretSpec secret '%s' must use 'as_path'", name);
    return AbsolutePath{secret.value};
}

} // namespace nix
