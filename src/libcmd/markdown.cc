#include "markdown.hh"
#include "util.hh"
#include "finally.hh"

#include <sys/queue.h>
#include <lowdown.h>

namespace nix {

std::string renderMarkdownToTerminal(std::string_view markdown)
{
    struct lowdown_opts opts {
        .type = LOWDOWN_TERM,
        .maxdepth = 20,
        .cols = std::max(getWindowSize().second, (unsigned short) 80),
        .hmargin = 0,
        .vmargin = 0,
        .feat = LOWDOWN_COMMONMARK | LOWDOWN_FENCED | LOWDOWN_DEFLIST | LOWDOWN_TABLES,
        .oflags = 0,
    };

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

    return filterANSIEscapes(std::string(buf->data, buf->size), !shouldANSI());
}

}
