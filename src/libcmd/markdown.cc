#include "nix/cmd/markdown.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/error.hh"
#include "nix/util/finally.hh"
#include "nix/util/terminal.hh"

#include "cmd-config-private.hh"

#if HAVE_LOWDOWN
#  include <sys/queue.h>
#  include <lowdown.h>
#endif

namespace nix {

#if HAVE_LOWDOWN
static std::string doRenderMarkdownToTerminal(std::string_view markdown)
{
    int windowWidth = getWindowSize().second;

#  if HAVE_LOWDOWN_1_4
    struct lowdown_opts_term opts_term{
        .cols = (size_t) std::max(windowWidth - 5, 60),
        .hmargin = 0,
        .vmargin = 0,
    };
#  endif
    struct lowdown_opts opts{
        .type = LOWDOWN_TERM,
#  if HAVE_LOWDOWN_1_4
        .term = opts_term,
#  endif
        .maxdepth = 20,
#  if !HAVE_LOWDOWN_1_4
        .cols = (size_t) std::max(windowWidth - 5, 60),
        .hmargin = 0,
        .vmargin = 0,
#  endif
        .feat = LOWDOWN_COMMONMARK | LOWDOWN_FENCED | LOWDOWN_DEFLIST | LOWDOWN_TABLES,
        .oflags =
#  if HAVE_LOWDOWN_1_4
            LOWDOWN_TERM_NORELLINK // To render full links while skipping relative ones
#  else
            LOWDOWN_TERM_NOLINK
#  endif
    };

    if (!isTTY())
        opts.oflags |= LOWDOWN_TERM_NOANSI;

    auto doc = lowdown_doc_new(&opts);
    if (!doc)
        throw Error("cannot allocate Markdown document");
    Finally freeDoc([&]() { lowdown_doc_free(doc); });

    size_t maxn = 0;
    auto node = lowdown_doc_parse(doc, &maxn, markdown.data(), markdown.size(), nullptr);
    if (!node)
        throw Error("cannot parse Markdown document");
    Finally freeNode([&]() { lowdown_node_free(node); });

    auto renderer = lowdown_term_new(&opts);
    if (!renderer)
        throw Error("cannot allocate Markdown renderer");
    Finally freeRenderer([&]() { lowdown_term_free(renderer); });

    auto buf = lowdown_buf_new(16384);
    if (!buf)
        throw Error("cannot allocate Markdown output buffer");
    Finally freeBuffer([&]() { lowdown_buf_free(buf); });

    int rndr_res = lowdown_term_rndr(buf, renderer, node);
    if (!rndr_res)
        throw Error("allocation error while rendering Markdown");

    return std::string(buf->data, buf->size);
}

std::string renderMarkdownToTerminal(std::string_view markdown)
{
    if (auto e = getEnv("_NIX_TEST_RAW_MARKDOWN"); e && *e == "1")
        return std::string(markdown);
    else
        return doRenderMarkdownToTerminal(markdown);
}

#else
std::string renderMarkdownToTerminal(std::string_view markdown)
{
    return std::string(markdown);
}
#endif

} // namespace nix
