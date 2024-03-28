#include "signature/signer.hh"
#include "network/user-agent.hh"
#include "error.hh"

#include <curl/curl.h>
#include <sodium.h>

namespace nix {

LocalSigner::LocalSigner(SecretKey && privateKey)
    : privateKey(privateKey)
    , publicKey(privateKey.toPublicKey())
{ }

std::string LocalSigner::signDetached(std::string_view s) const
{
    return privateKey.signDetached(s);
}

const PublicKey & LocalSigner::getPublicKey()
{
    return publicKey;
}

RemoteSigner::RemoteSigner(const std::string & remoteServerPath)
    : Signer()
    , serverPath(remoteServerPath)
    , _curl_handle(std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(
        curl_easy_init(),
        curl_easy_cleanup))
{
    if (!_curl_handle) {
        throw Error("Failed to initialize curl");
    }

    set_user_agent(_curl_handle.get(), "");
    // Signing should be very fast, 5s is already very long.
    curl_easy_setopt(_curl_handle.get(), CURLOPT_TIMEOUT, 5);

    // The remote URL should not have no trailing /.
    if (serverPath.back() == '/') {
        throw Error("Remote signing path `%s` contains a trailing `/`", serverPath);
    }

    curl_easy_setopt(_curl_handle.get(), CURLOPT_UNIX_SOCKET_PATH, serverPath.c_str());
}

// Write the HTTP response inside the userdata string buffer provided.
static size_t _writeResponse(void * ptr, size_t size, size_t nmemb, void * userdata) {
    size_t real_size = size * nmemb;
    std::string * response = static_cast<std::string *>(userdata);
    response->append(static_cast<const char *>(ptr), real_size);
    return real_size;
}

const PublicKey & RemoteSigner::getPublicKey()
{
    if (optPublicKey) return *optPublicKey;

    if (!_curl_handle) {
        throw Error("CURL is not initialized!");
    }

    // Since cURL 7.50, a valid URL must always be passed, we use a
    // dummy hostname here.

    std::string responseSlot;

    curl_easy_setopt(_curl_handle.get(), CURLOPT_URL, "http://localhost/publickey");
    curl_easy_setopt(_curl_handle.get(), CURLOPT_WRITEFUNCTION, &_writeResponse);
    curl_easy_setopt(_curl_handle.get(), CURLOPT_WRITEDATA, &responseSlot);

    CURLcode res = curl_easy_perform(_curl_handle.get());
    if (res != CURLE_OK) {
        // log the error?
        throw Error("failed to fetch the remote public key (curl error)");
    }

    long http_code = 0;
    curl_easy_getinfo(_curl_handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        throw Error("failed to fetch the remote public key (non-200 error code from server)");
    }

    // One we made it to here, we know we have a correct value to parse
    // and remember

    optPublicKey = PublicKey { std::string_view { responseSlot } };

    return *optPublicKey;
}

std::string RemoteSigner::signDetached(std::string_view fingerprint) const
{
    std::string detached_signature;

    // Since cURL 7.50, a valid URL must always be passed, we use a dummy hostname here.
    curl_easy_setopt(_curl_handle.get(), CURLOPT_URL, "http://localhost/sign");
    curl_easy_setopt(_curl_handle.get(), CURLOPT_WRITEFUNCTION, &_writeResponse);
    curl_easy_setopt(_curl_handle.get(), CURLOPT_WRITEDATA, &detached_signature);
    curl_easy_setopt(_curl_handle.get(), CURLOPT_POSTFIELDS, fingerprint.data());

    CURLcode res = curl_easy_perform(_curl_handle.get());
    if (res != CURLE_OK) {
        // log the error?
        throw Error("failed to sign remotely (curl error)");
    }

    long http_code = 0;
    curl_easy_getinfo(_curl_handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        throw Error("failed to sign remotely (non-200 error code from server)");
    }

    return detached_signature;
}

}
