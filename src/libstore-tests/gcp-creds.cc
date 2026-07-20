#include "nix/store/gcp-creds.hh"

#if NIX_WITH_GCS_AUTH

#  include "nix/util/base-n.hh"
#  include "nix/util/environment-variables.hh"
#  include "nix/util/file-system.hh"

#  include <gtest/gtest.h>
#  include <nlohmann/json.hpp>
#  include <openssl/evp.h>
#  include <openssl/pem.h>

namespace nix {

using namespace gcp_detail;

namespace {

/** Reverse of `gcp_detail::base64url` for inspecting JWT segments in tests. */
std::string base64urlDecode(std::string s)
{
    for (auto & c : s) {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
    }
    while (s.size() % 4 != 0)
        s.push_back('=');
    return base64::decode(s);
}

/** RAII env-var override that restores the previous value on destruction. */
struct ScopedEnv
{
    std::string name;
    std::optional<std::string> prev;

    ScopedEnv(const char * name_, std::optional<std::string> value)
        : name(name_)
        , prev(getEnv(name_))
    {
        if (value)
            setEnv(name_, value->c_str());
        else
            unsetenv(name_);
    }

    ~ScopedEnv()
    {
        if (prev)
            setEnv(name.c_str(), prev->c_str());
        else
            unsetenv(name.c_str());
    }
};

/** Generate a throwaway RSA keypair and return (private PEM, EVP_PKEY*). */
std::pair<std::string, std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>> generateRsaKey()
{
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(EVP_RSA_gen(2048), EVP_PKEY_free); // OpenSSL 3 helper
    if (!pkey)
        throw std::runtime_error("EVP_RSA_gen failed");

    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);
    char * data = nullptr;
    long len = BIO_get_mem_data(bio.get(), &data);
    return {std::string(data, len), std::move(pkey)};
}

bool verifyRS256(EVP_PKEY * pkey, std::string_view payload, std::string_view sig)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pkey);
    EVP_DigestVerifyUpdate(ctx.get(), payload.data(), payload.size());
    return EVP_DigestVerifyFinal(ctx.get(), reinterpret_cast<const unsigned char *>(sig.data()), sig.size()) == 1;
}

} // anonymous namespace

TEST(GcpCreds, base64urlUsesUrlAlphabet)
{
    // 0xfb 0xff 0xfe → standard base64 "+//+"; URL alphabet swaps +/ for -_
    // and drops padding, so we must get exactly "-__-".
    std::byte raw[]{std::byte{0xfb}, std::byte{0xff}, std::byte{0xfe}};
    EXPECT_EQ(base64url(std::span{raw}), "-__-");
    // Padding stripped for non-multiple-of-3 input.
    EXPECT_EQ(base64url("fo"), "Zm8");
}

TEST(GcpCreds, parseTokenResponseHappyPath)
{
    auto before = std::chrono::steady_clock::now();
    auto creds = parseTokenResponse(R"({"access_token":"ya29.abc","expires_in":3600,"token_type":"Bearer"})");
    auto after = std::chrono::steady_clock::now();

    EXPECT_EQ(creds.accessToken, "ya29.abc");
    // expires_in=3600 minus 225s slack ⇒ ~56 minutes from now.
    EXPECT_GT(creds.expiresAt, before + std::chrono::minutes(55));
    EXPECT_LT(creds.expiresAt, after + std::chrono::minutes(58));
}

TEST(GcpCreds, parseTokenResponseClampsShortExpiry)
{
    auto before = std::chrono::steady_clock::now();
    auto creds = parseTokenResponse(R"({"access_token":"t","expires_in":60})");
    // 60s - 225s would be in the past; clamp to ≥30s.
    EXPECT_GE(creds.expiresAt, before + std::chrono::seconds(30));
    EXPECT_LT(creds.expiresAt, before + std::chrono::seconds(60));
}

TEST(GcpCreds, parseTokenResponseRejectsMissingToken)
{
    EXPECT_THROW(parseTokenResponse(R"({"expires_in":3600})"), GcpAuthError);
}

TEST(GcpCreds, parseTokenResponseRejectsBadJson)
{
    EXPECT_THROW(parseTokenResponse("not json"), GcpAuthError);
}

TEST(GcpCreds, buildServiceAccountJwtSigningInput)
{
    auto input = buildServiceAccountJwtSigningInput(
        "sa@project.iam.gserviceaccount.com", "https://oauth2.googleapis.com/token", 1000, 4600);

    auto dot = input.find('.');
    ASSERT_NE(dot, std::string::npos);
    auto header = nlohmann::json::parse(base64urlDecode(input.substr(0, dot)));
    auto claims = nlohmann::json::parse(base64urlDecode(input.substr(dot + 1)));

    EXPECT_EQ(header["alg"], "RS256");
    EXPECT_EQ(header["typ"], "JWT");
    EXPECT_EQ(claims["iss"], "sa@project.iam.gserviceaccount.com");
    EXPECT_EQ(claims["aud"], "https://oauth2.googleapis.com/token");
    EXPECT_EQ(claims["scope"], "https://www.googleapis.com/auth/devstorage.read_write");
    EXPECT_EQ(claims["iat"], 1000);
    EXPECT_EQ(claims["exp"], 4600);
}

TEST(GcpCreds, signRS256RoundTrip)
{
    auto [pem, pkey] = generateRsaKey();
    constexpr std::string_view payload = "header.claims";

    auto sig = signRS256(pem, payload);
    EXPECT_FALSE(sig.empty());
    EXPECT_TRUE(verifyRS256(pkey.get(), payload, sig));
    // Tampered payload must not verify.
    EXPECT_FALSE(verifyRS256(pkey.get(), "header.other", sig));
}

TEST(GcpCreds, signRS256RejectsBadPem)
{
    EXPECT_THROW(signRS256("not a key", "payload"), GcpAuthError);
}

TEST(GcpCreds, findAdcFilePrefersExplicitEnv)
{
    ScopedEnv gac("GOOGLE_APPLICATION_CREDENTIALS", "/nonexistent/creds.json");
    ScopedEnv cdk("CLOUDSDK_CONFIG", std::nullopt);

    auto found = findAdcFile();
    // The env var wins regardless of whether the file exists; the caller is
    // responsible for surfacing a useful error if it doesn't.
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->string(), "/nonexistent/creds.json");
}

TEST(GcpCreds, findAdcFileFallsBackToCloudSdkConfig)
{
    auto tmp = createTempDir();
    auto adc = tmp / "application_default_credentials.json";
    writeFile(adc, "{}");

    ScopedEnv gac("GOOGLE_APPLICATION_CREDENTIALS", std::nullopt);
    ScopedEnv cdk("CLOUDSDK_CONFIG", tmp.string());

    auto found = findAdcFile();
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, adc);

    deletePath(tmp);
}

TEST(GcpCreds, findAdcFileNoneAvailable)
{
    auto tmp = createTempDir(); // empty dir, no ADC file inside

    ScopedEnv gac("GOOGLE_APPLICATION_CREDENTIALS", std::nullopt);
    ScopedEnv cdk("CLOUDSDK_CONFIG", tmp.string());

    EXPECT_FALSE(findAdcFile().has_value());

    deletePath(tmp);
}

} // namespace nix

#endif
