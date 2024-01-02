#include "signature/signer.hh"
#include "network/user-agent.hh"
#include "util.hh"

#include <curl/curl.h>
#include <sodium.h>

namespace nix {

// Splits a signature, that is `$publicKey:$signatureDigest` into a pair (publicKey, signatureDigest)
static std::pair<std::string_view, std::string_view> split(std::string_view s)
{
    size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0)
        return {"", ""};
    return {s.substr(0, colon), s.substr(colon + 1)};
}

Signer::Signer() : pubkey(this->getPublicKey()) { }
Signer::Signer(PublicKey && pubkey) : pubkey(pubkey) { }

bool Signer::verifyDetached(std::string_view data, std::string_view sig) {
    auto ss = split(sig);

    if (std::string(ss.first) != pubkey.key) return false;

    auto sig2 = base64Decode(ss.second);
    if (sig2.size() != crypto_sign_BYTES)
        throw Error("signature is not valid");

    return crypto_sign_verify_detached((unsigned char *) sig2.data(),
        (unsigned char *) data.data(), data.size(),
        (unsigned char *) pubkey.key.data()) == 0;
}

const PublicKey& Signer::getPublicKey() const {
    return pubkey;
}

LocalSigner::LocalSigner(SecretKey && privkey) : Signer(privkey.toPublicKey()), privkey(privkey) {
}

std::string LocalSigner::signDetached(std::string_view s) const {
    return privkey.signDetached(s);
}

RemoteSigner::RemoteSigner(const std::string & remoteServerPath) : Signer(),
    serverPath(remoteServerPath),
    _curl_handle(std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup)) {
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
static size_t _writeResponse(void* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t real_size = size *nmemb;
    std::string* response = static_cast<std::string*>(userdata);
    response->append(static_cast<char*>(ptr), real_size);
    return real_size;
}

void RemoteSigner::fetchAndRememberPublicKey() {
    if (!_curl_handle) {
        throw Error("CURL is not initialized!");
    }

    // Since cURL 7.50, a valid URL must always be passed, we use a dummy hostname here.
    curl_easy_setopt(_curl_handle.get(), CURLOPT_URL, "http://localhost/publickey");
    curl_easy_setopt(_curl_handle.get(), CURLOPT_WRITEFUNCTION, &_writeResponse);
    curl_easy_setopt(_curl_handle.get(), CURLOPT_WRITEDATA, &this->pubkey);

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
}

std::string RemoteSigner::signDetached(std::string_view fingerprint) const {
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
