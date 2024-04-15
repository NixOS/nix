#include "markdown.hh"
#include "util.hh"
#include "terminal.hh"

#if HAVE_LOWDOWN
# include "lowdown-cpp.hh"
#endif

namespace nix {

std::string renderMarkdownToTerminal(std::string_view markdown)
{
#if HAVE_LOWDOWN
    int windowWidth = getWindowSize().second;

    struct lowdown_opts opts {
        .type = LOWDOWN_TERM,
        .maxdepth = 20,
        .cols = (size_t) std::max(windowWidth - 5, 60),
        .hmargin = 0,
        .vmargin = 0,
        .feat = LOWDOWN_COMMONMARK | LOWDOWN_FENCED | LOWDOWN_DEFLIST | LOWDOWN_TABLES,
        .oflags = LOWDOWN_TERM_NOLINK,
    };

    auto doc = lowdown::UniquePtr<lowdown::Doc> {
        lowdown_doc_new(&opts),
    };
    if (!doc)
        throw Error("cannot allocate Markdown document");

    size_t maxn = 0;
    auto node = lowdown::UniquePtr<lowdown::Node> {
        lowdown_doc_parse(&*doc, &maxn, markdown.data(), markdown.size(), nullptr),
    };
    if (!node)
        throw Error("cannot parse Markdown document");

    auto renderer = lowdown::UniquePtr<lowdown::Term> {
        reinterpret_cast<lowdown::Term *>(lowdown_term_new(&opts)),
    };
    if (!renderer)
        throw Error("cannot allocate Markdown renderer");

    auto buf = lowdown::UniquePtr<struct lowdown_buf> {
        lowdown_buf_new(16384),
    };
    if (!buf)
        throw Error("cannot allocate Markdown output buffer");

    int rndr_res = lowdown_term_rndr(&*buf, &*renderer, &*node);
    if (!rndr_res)
        throw Error("allocation error while rendering Markdown");

    return filterANSIEscapes(std::string(buf->data, buf->size), !isTTY());
#else
    return std::string(markdown);
#endif
}

}
