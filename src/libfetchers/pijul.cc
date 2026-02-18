// #include "cache.hh"
#include "fetchers.hh"
#include "store-api.hh"

// #include "fetch-settings.hh"

#include <string.h>

using namespace std::string_literals;

namespace nix::fetchers {

struct PijulInputScheme : InputScheme {
  std::optional<Input> inputFromURL(const ParsedURL &url) const override {
    if (url.scheme != "pijul+http" && url.scheme != "pijul+https")
      return {};

    auto url2(url);
    url2.scheme = std::string(url2.scheme, 6);
    url2.query.clear();

    Attrs attrs;
    attrs.emplace("type", "pijul");
    attrs.emplace("url", url2.to_string());

    return inputFromAttrs(attrs);
  }

  std::optional<Input> inputFromAttrs(const Attrs &attrs) const override {
    if (maybeGetStrAttr(attrs, "type") != "pijul")
      return {};

    for (auto &[name, _] : attrs)
      if (name != "type" && name != "url")
        throw Error("unsupported Pijul input attribute '%s'", name);

    parseURL(getStrAttr(attrs, "url"));

    Input input;
    input.attrs = attrs;
    return input;
  }

  bool hasAllInfo(const Input &input) const override { return true; }

  ParsedURL toURL(const Input &input) const override {
    auto url = parseURL(getStrAttr(input.attrs, "url"));
    if (url.scheme != "pijul")
      url.scheme = "pijul+" + url.scheme;
    return url;
  }

  std::pair<StorePath, Input> fetch(ref<Store> store,
                                    const Input &_input) override {
    Input input(_input);

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto repoDir = tmpDir + "/source";

    auto url = parseURL(getStrAttr(input.attrs, "url"));
    auto repoUrl = url.base;

    runProgram("pijul", true, {"clone", repoUrl, repoDir}, {}, true);
    deletePath(repoDir + "/.pijul");

    auto storePath = store->addToStore(input.getName(), repoDir);

    return {std::move(storePath), input};
  }
};

static auto rPijulInputScheme = OnStartup(
    [] { registerInputScheme(std::make_unique<PijulInputScheme>()); });

} // namespace nix::fetchers
