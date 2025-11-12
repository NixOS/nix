/**
 * Terminal renderer for CommonMark documents
 *
 * Ported from lowdown's term.c by Kristaps Dzonsons
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
 * Adapted for cmark by the Nix project
 */

#include "cmark/cmark-terminal.hh"

#include <cassert>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <sstream>
#include <stack>
#include <map>

namespace cmark {

// ============================================================================
// Style Definitions (from term.h)
// ============================================================================

// Inline styles (only those supported by CMark)
static const Style sty_img = {false, false, true, false, 0, 93, Style::OVERRIDE_BOLD};
static const Style sty_imgbox = {false, false, false, false, 0, 37, Style::OVERRIDE_BOLD};
static const Style sty_imgurl = {false, false, false, true, 0, 32, Style::OVERRIDE_BOLD};
static const Style sty_codespan = {false, false, true, false, 0, 94, 0};
static const Style sty_blockcode = {false, false, true, false, 0, 0, 0};
static const Style sty_hrule = {false, false, false, false, 0, 37, 0};
static const Style sty_blockhtml = {false, false, false, false, 0, 37, 0};
static const Style sty_rawhtml = {false, false, false, false, 0, 37, 0};
static const Style sty_emph = {true, false, false, false, 0, 0, 0};
static const Style sty_d_emph = {false, false, true, false, 0, 0, 0};
static const Style sty_link = {false, false, false, true, 0, 32, 0};
static const Style sty_linkalt = {false, false, true, false, 0, 93, Style::OVERRIDE_UNDER | Style::OVERRIDE_BOLD};
static const Style sty_header = {false, false, true, false, 0, 0, 0};
static const Style sty_header_1 = {false, false, false, false, 0, 91, 0};
static const Style sty_header_n = {false, false, false, false, 0, 36, 0};

// Prefix styles
static const Style sty_li_pfx = {false, false, false, false, 0, 93, 0};
static const Style sty_bkqt_pfx = {false, false, false, false, 0, 93, 0};
static const Style sty_bkcd_pfx = {false, false, false, false, 0, 94, 0};

// Prefix strings
struct Prefix
{
    const char * text;
    size_t cols;
};

static const Prefix pfx_bkcd = {"  │ ", 4};
static const Prefix pfx_bkqt = {"  │ ", 4};
static const Prefix pfx_oli_1 = {nullptr, 4};
static const Prefix pfx_uli_1 = {"  · ", 4};
static const Prefix pfx_li_n = {"    ", 4};
static const Prefix pfx_header_1 = {"", 0};
static const Prefix pfx_header_n = {"#", 1};

// Infixes
static const char * ifx_hrule = "─";
static const char * ifx_imgbox_left = "[🖻 ";
static const char * ifx_imgbox_right = "]";
static const char * ifx_imgbox_sep = " ";
static const char * ifx_link_sep = " ";

// ============================================================================
// Implementation
// ============================================================================

struct NodeStack
{
    const cmark::Node * n;
    size_t lines;
};

struct TerminalRenderer::Impl
{
    TerminalOptions opts;
    size_t col = 0;
    ssize_t last_blank = -1;
    std::vector<NodeStack> stack;
    size_t width = 0;
    size_t hmargin = 0;
    size_t hpadding = 0;
    size_t vmargin = 0;
    std::string tmp;
    std::vector<wchar_t> buf;
    const cmark::Node * in_link = nullptr;

    Impl(const TerminalOptions & options)
        : opts(options)
    {
        // Compute the width of the content
        if (opts.width == 0) {
            width = opts.cols > 80 ? 80 : opts.cols;
        } else if (opts.width > opts.cols) {
            width = opts.cols;
        } else {
            width = opts.width;
        }

        // Compute the horizontal margin
        if (opts.centre && width < opts.cols) {
            hmargin = (opts.cols - width) / 2;
        } else {
            hmargin = opts.hmargin;
        }

        hpadding = opts.hpadding;
        vmargin = opts.vmargin;

        // Adjust width for padding
        if (hpadding >= width) {
            width = 1;
        } else {
            width -= hpadding;
        }
    }

    // Get the column width of a multi-byte sequence
    ssize_t mbsWidth(const char * data, size_t sz);

    // Output style to string
    void putStyle(std::string & out, const Style & s);
    void putUnstyle(std::string & out, const Style * s);

    // OSC8 hyperlink support
    void putOsc8Open(std::string & out, const cmark::Node & n);
    void putOsc8Close(std::string & out);

    // Line and word management
    void advance(size_t len);
    bool startLine(std::string & out, const cmark::Node & n, const Style * osty = nullptr);
    bool endLine(std::string & out, const cmark::Node & n, const Style * osty = nullptr);
    bool startWords(std::string & out, const cmark::Node & n, const Style * osty = nullptr);
    bool endWords(std::string & out, const cmark::Node & n, const Style * osty = nullptr);

    // Rendering functions
    bool vspace(std::string & out, const cmark::Node & n, size_t sz);
    ssize_t escape(std::string & out, const char * data, size_t sz);
    bool
    renderBuf(std::string & out, const cmark::Node & n, const char * data, size_t sz, const Style * osty = nullptr);
    bool
    renderLiteral(std::string & out, const cmark::Node & n, const char * data, size_t sz, const Style * osty = nullptr);
    bool renderHrule(std::string & out, const cmark::Node & n, const char * hr, const Style * sty = nullptr);

    // Node rendering
    bool render(std::string & out, const cmark::Node & n);

    // Style management
    void getNodeStyle(Style & s, const cmark::Node & n);
    void applyStyle(Style & to, const Style & from);
    bool hasEndStyle(const cmark::Node & n);

    // Prefix rendering
    bool renderPrefixes(std::string & out, Style & s, const cmark::Node & n, size_t & depth);
};

// ============================================================================
// Helper Functions
// ============================================================================

ssize_t TerminalRenderer::Impl::mbsWidth(const char * data, size_t sz)
{
    std::mbstate_t mbs{};
    const char * cp = data;
    size_t wsz = mbsnrtowcs(nullptr, &cp, sz, 0, &mbs);

    if (wsz == static_cast<size_t>(-1)) {
        return sz;
    }

    if (buf.size() < wsz) {
        buf.resize(wsz);
    }

    mbs = {};
    cp = data;
    mbsnrtowcs(buf.data(), &cp, sz, wsz, &mbs);
    size_t csz = wcswidth(buf.data(), wsz);
    return csz == static_cast<size_t>(-1) ? sz : csz;
}

void TerminalRenderer::Impl::putStyle(std::string & out, const Style & s)
{
    if (opts.noAnsi || !s.hasStyle()) {
        return;
    }

    out += "\033[";
    bool has = false;

    if (s.bold) {
        out += "1";
        has = true;
    }
    if (s.under) {
        if (has)
            out += ";";
        out += "4";
        has = true;
    }
    if (s.italic) {
        if (has)
            out += ";";
        out += "3";
        has = true;
    }
    if (s.strike) {
        if (has)
            out += ";";
        out += "9";
        has = true;
    }
    if (s.bcolour && !opts.noColor
        && ((s.bcolour >= 40 && s.bcolour <= 47) || (s.bcolour >= 100 && s.bcolour <= 107))) {
        if (has)
            out += ";";
        out += std::to_string(s.bcolour);
        has = true;
    }
    if (s.colour && !opts.noColor && ((s.colour >= 30 && s.colour <= 37) || (s.colour >= 90 && s.colour <= 97))) {
        if (has)
            out += ";";
        out += std::to_string(s.colour);
        has = true;
    }
    out += "m";
}

void TerminalRenderer::Impl::putUnstyle(std::string & out, const Style * s)
{
    if (opts.noAnsi) {
        return;
    }
    if (s && !s->hasStyle()) {
        return;
    }
    out += "\033[0m";
}

void TerminalRenderer::Impl::putOsc8Open(std::string & out, const cmark::Node & n)
{
    if (opts.noAnsi) {
        return;
    }

    auto type = cmark_node_get_type(const_cast<cmark::Node *>(&n));
    const char * url = nullptr;

    if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE) {
        url = cmark_node_get_url(const_cast<cmark::Node *>(&n));
    }

    if (!url) {
        return;
    }

    out += "\033]8;;";
    out += url;
    out += "\033\\";
}

void TerminalRenderer::Impl::putOsc8Close(std::string & out)
{
    if (opts.noAnsi) {
        return;
    }
    out += "\033]8;;\033\\";
}

void TerminalRenderer::Impl::advance(size_t len)
{
    col += len;
    if (col && last_blank != 0) {
        last_blank = 0;
    }
}

void TerminalRenderer::Impl::applyStyle(Style & to, const Style & from)
{
    if (from.italic)
        to.italic = true;
    if (from.strike)
        to.strike = true;
    if (from.bold)
        to.bold = true;
    else if (from.override & Style::OVERRIDE_BOLD)
        to.bold = false;

    if (from.under)
        to.under = true;
    else if (from.override & Style::OVERRIDE_UNDER)
        to.under = false;

    if (from.bcolour)
        to.bcolour = from.bcolour;
    if (from.colour)
        to.colour = from.colour;
}

void TerminalRenderer::Impl::getNodeStyle(Style & s, const cmark::Node & n)
{
    auto type = cmark_node_get_type(const_cast<cmark::Node *>(&n));

    switch (type) {
    case CMARK_NODE_CODE_BLOCK:
        applyStyle(s, sty_blockcode);
        break;
    case CMARK_NODE_HTML_BLOCK:
        applyStyle(s, sty_blockhtml);
        break;
    case CMARK_NODE_CODE:
        applyStyle(s, sty_codespan);
        break;
    case CMARK_NODE_EMPH:
        applyStyle(s, sty_emph);
        break;
    case CMARK_NODE_STRONG:
        applyStyle(s, sty_d_emph);
        break;
    case CMARK_NODE_LINK:
        applyStyle(s, sty_link);
        break;
    case CMARK_NODE_IMAGE:
        applyStyle(s, sty_img);
        break;
    case CMARK_NODE_HTML_INLINE:
        applyStyle(s, sty_rawhtml);
        break;
    case CMARK_NODE_HEADING: {
        applyStyle(s, sty_header);
        int level = cmark_node_get_heading_level(const_cast<cmark::Node *>(&n));
        if (level == 1) {
            applyStyle(s, sty_header_1);
        } else {
            applyStyle(s, sty_header_n);
        }
        break;
    }
    case CMARK_NODE_THEMATIC_BREAK:
        applyStyle(s, sty_hrule);
        break;
    case CMARK_NODE_NONE:
    case CMARK_NODE_DOCUMENT:
    case CMARK_NODE_BLOCK_QUOTE:
    case CMARK_NODE_LIST:
    case CMARK_NODE_ITEM:
    case CMARK_NODE_CUSTOM_BLOCK:
    case CMARK_NODE_PARAGRAPH:
    case CMARK_NODE_TEXT:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
    case CMARK_NODE_CUSTOM_INLINE:
        // No special styling
        break;
    }

    // Apply linkalt style for children of links
    auto parent = cmark_node_parent(const_cast<cmark::Node *>(&n));
    if (parent && cmark_node_get_type(parent) == CMARK_NODE_LINK) {
        applyStyle(s, sty_linkalt);
    }
}

bool TerminalRenderer::Impl::hasEndStyle(const cmark::Node & n)
{
    auto parent = cmark_node_parent(const_cast<cmark::Node *>(&n));
    if (parent && hasEndStyle(*parent)) {
        return true;
    }

    Style s{};
    getNodeStyle(s, n);
    return s.hasStyle();
}

ssize_t TerminalRenderer::Impl::escape(std::string & out, const char * data, size_t sz)
{
    size_t start = 0;
    size_t cols = 0;

    for (size_t i = 0; i < sz; i++) {
        unsigned char ch = data[i];
        if (ch < 0x80 && std::iscntrl(ch)) {
            ssize_t ret = mbsWidth(data + start, i - start);
            if (ret < 0)
                return -1;
            cols += ret;
            out.append(data + start, i - start);
            start = i + 1;
        }
    }

    if (start < sz) {
        ssize_t ret = mbsWidth(data + start, sz - start);
        if (ret < 0)
            return -1;
        cols += ret;
        out.append(data + start, sz - start);
    }

    return cols;
}

static bool isRelativeLink(const char * link)
{
    if (!link)
        return false;

    const char * colon = std::strchr(link, ':');
    if (!colon)
        return true;

    // Check if there's a slash before the colon
    const char * slash = std::strchr(link, '/');
    return slash && slash < colon;
}

static size_t numLen(size_t sz)
{
    if (sz > 100000)
        return 6;
    if (sz > 10000)
        return 5;
    if (sz > 1000)
        return 4;
    if (sz > 100)
        return 3;
    if (sz > 10)
        return 2;
    return 1;
}

// Render prefixes for the current line
bool TerminalRenderer::Impl::renderPrefixes(std::string & out, Style & s, const cmark::Node & n, size_t & depth)
{
    auto parent = cmark_node_parent(const_cast<cmark::Node *>(&n));
    if (parent) {
        if (!renderPrefixes(out, s, *parent, depth)) {
            return false;
        }
    } else {
        assert(cmark_node_get_type(const_cast<cmark::Node *>(&n)) == CMARK_NODE_DOCUMENT);
        depth = 0;
    }

    getNodeStyle(s, n);
    Style sinner = s;

    auto type = cmark_node_get_type(const_cast<cmark::Node *>(&n));

    // Find current node in stack
    size_t emit = 0;
    for (size_t i = 0; i < stack.size(); i++) {
        if (stack[i].n == &n) {
            emit = stack[i].lines++;
            break;
        }
    }

    bool pstyle = false;
    const Prefix * pfx = nullptr;

    switch (type) {
    case CMARK_NODE_CODE_BLOCK:
        applyStyle(sinner, sty_bkcd_pfx);
        putStyle(out, sinner);
        pstyle = true;
        out += pfx_bkcd.text;
        advance(pfx_bkcd.cols);
        break;

    case CMARK_NODE_DOCUMENT:
        putStyle(out, sinner);
        pstyle = true;
        for (size_t i = 0; i < hmargin; i++)
            out += " ";
        for (size_t i = 0; i < hpadding; i++)
            out += " ";
        break;

    case CMARK_NODE_BLOCK_QUOTE:
        applyStyle(sinner, sty_bkqt_pfx);
        putStyle(out, sinner);
        pstyle = true;
        out += pfx_bkqt.text;
        advance(pfx_bkqt.cols);
        break;

    case CMARK_NODE_HEADING: {
        int level = cmark_node_get_heading_level(const_cast<cmark::Node *>(&n));
        pfx = (level == 1) ? &pfx_header_1 : &pfx_header_n;
        putStyle(out, sinner);
        pstyle = true;
        for (int i = 0; i < level; i++) {
            if (pfx->text)
                out += pfx->text;
            advance(pfx->cols);
        }
        if (pfx->cols) {
            out += " ";
            advance(1);
        }
        break;
    }

    case CMARK_NODE_ITEM: {
        if (emit) {
            out += pfx_li_n.text;
            advance(pfx_li_n.cols);
            break;
        }

        auto list_parent = parent;
        if (list_parent && cmark_node_get_type(list_parent) == CMARK_NODE_LIST) {
            auto list_type = cmark_node_get_list_type(list_parent);
            applyStyle(sinner, sty_li_pfx);
            putStyle(out, sinner);
            pstyle = true;

            if (list_type == CMARK_ORDERED_LIST) {
                int start = cmark_node_get_list_start(list_parent);
                // Calculate item number
                int item_num = start;
                auto sibling = cmark_node_first_child(list_parent);
                while (sibling && sibling != &n) {
                    item_num++;
                    sibling = cmark_node_next(sibling);
                }

                char numbuf[32];
                std::snprintf(numbuf, sizeof(numbuf), "%2d. ", item_num);
                out += numbuf;
                size_t len = numLen(item_num);
                if (len + 2 > pfx_oli_1.cols) {
                    advance(len + 2);
                } else {
                    advance(pfx_oli_1.cols);
                }
            } else {
                out += pfx_uli_1.text;
                advance(pfx_uli_1.cols);
            }
        }
        break;
    }

    case CMARK_NODE_NONE:
    case CMARK_NODE_LIST:
    case CMARK_NODE_HTML_BLOCK:
    case CMARK_NODE_CUSTOM_BLOCK:
    case CMARK_NODE_PARAGRAPH:
    case CMARK_NODE_THEMATIC_BREAK:
    case CMARK_NODE_TEXT:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
    case CMARK_NODE_CODE:
    case CMARK_NODE_HTML_INLINE:
    case CMARK_NODE_CUSTOM_INLINE:
    case CMARK_NODE_EMPH:
    case CMARK_NODE_STRONG:
    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
        // No prefix for these node types
        break;
    }

    if (pstyle) {
        putUnstyle(out, &sinner);
    }

    depth++;
    return true;
}

bool TerminalRenderer::Impl::startLine(std::string & out, const cmark::Node & n, const Style * osty)
{
    assert(last_blank);
    assert(col == 0);

    Style s{};
    size_t depth = 0;
    if (!renderPrefixes(out, s, n, depth)) {
        return false;
    }

    if (in_link) {
        putOsc8Open(out, *in_link);
    }

    if (osty) {
        applyStyle(s, *osty);
    }
    putStyle(out, s);
    return true;
}

bool TerminalRenderer::Impl::endWords(std::string & out, const cmark::Node & n, const Style * osty)
{
    if (hasEndStyle(n)) {
        putUnstyle(out, nullptr);
    }
    if (osty) {
        putUnstyle(out, osty);
    }
    if (in_link) {
        putOsc8Close(out);
    }
    return true;
}

bool TerminalRenderer::Impl::endLine(std::string & out, const cmark::Node & n, const Style * osty)
{
    if (!endWords(out, n, osty)) {
        return false;
    }

    col = 0;
    last_blank = 1;
    out += "\n";
    return true;
}

bool TerminalRenderer::Impl::vspace(std::string & out, const cmark::Node & n, size_t sz)
{
    if (last_blank == -1) {
        return true;
    }

    assert(sz > 0);
    while (static_cast<size_t>(last_blank) < sz) {
        if (col) {
            out += "\n";
        } else {
            auto parent = cmark_node_parent(const_cast<cmark::Node *>(&n));
            if (parent) {
                if (!startLine(out, *parent, nullptr))
                    return false;
                if (!endLine(out, *parent, nullptr))
                    return false;
            } else {
                out += "\n";
            }
        }
        last_blank++;
        col = 0;
    }
    return true;
}

static void applyNodeStyleToStyle(Style & to, const Style & from)
{
    if (from.italic)
        to.italic = true;
    if (from.strike)
        to.strike = true;
    if (from.bold)
        to.bold = true;
    else if (from.override & Style::OVERRIDE_BOLD)
        to.bold = false;

    if (from.under)
        to.under = true;
    else if (from.override & Style::OVERRIDE_UNDER)
        to.under = false;

    if (from.bcolour)
        to.bcolour = from.bcolour;
    if (from.colour)
        to.colour = from.colour;
}

static void getNodeStyleForType(cmark_node_type type, int heading_level, bool has_link_parent, Style & s)
{
    switch (type) {
    case CMARK_NODE_CODE_BLOCK:
        applyNodeStyleToStyle(s, sty_blockcode);
        break;
    case CMARK_NODE_HTML_BLOCK:
        applyNodeStyleToStyle(s, sty_blockhtml);
        break;
    case CMARK_NODE_CODE:
        applyNodeStyleToStyle(s, sty_codespan);
        break;
    case CMARK_NODE_EMPH:
        applyNodeStyleToStyle(s, sty_emph);
        break;
    case CMARK_NODE_STRONG:
        applyNodeStyleToStyle(s, sty_d_emph);
        break;
    case CMARK_NODE_LINK:
        applyNodeStyleToStyle(s, sty_link);
        break;
    case CMARK_NODE_IMAGE:
        applyNodeStyleToStyle(s, sty_img);
        break;
    case CMARK_NODE_HTML_INLINE:
        applyNodeStyleToStyle(s, sty_rawhtml);
        break;
    case CMARK_NODE_HEADING:
        applyNodeStyleToStyle(s, sty_header);
        if (heading_level == 1) {
            applyNodeStyleToStyle(s, sty_header_1);
        } else {
            applyNodeStyleToStyle(s, sty_header_n);
        }
        break;
    case CMARK_NODE_THEMATIC_BREAK:
        applyNodeStyleToStyle(s, sty_hrule);
        break;
    case CMARK_NODE_NONE:
    case CMARK_NODE_DOCUMENT:
    case CMARK_NODE_BLOCK_QUOTE:
    case CMARK_NODE_LIST:
    case CMARK_NODE_ITEM:
    case CMARK_NODE_CUSTOM_BLOCK:
    case CMARK_NODE_PARAGRAPH:
    case CMARK_NODE_TEXT:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
    case CMARK_NODE_CUSTOM_INLINE:
        // No special styling
        break;
    }

    if (has_link_parent) {
        applyNodeStyleToStyle(s, sty_linkalt);
    }
}

static void getStartWordsStyle(const cmark::Node & n, Style & s)
{
    auto parent = cmark_node_parent(const_cast<cmark::Node *>(&n));
    if (parent) {
        getStartWordsStyle(*parent, s);
    }

    auto type = cmark_node_get_type(const_cast<cmark::Node *>(&n));
    int heading_level = (type == CMARK_NODE_HEADING) ? cmark_node_get_heading_level(const_cast<cmark::Node *>(&n)) : 0;
    bool has_link_parent = parent && cmark_node_get_type(parent) == CMARK_NODE_LINK;

    getNodeStyleForType(type, heading_level, has_link_parent, s);
}

bool TerminalRenderer::Impl::startWords(std::string & out, const cmark::Node & n, const Style * osty)
{
    if (in_link) {
        putOsc8Open(out, *in_link);
    }

    assert(!last_blank);
    assert(col > 0);

    Style s{};
    getStartWordsStyle(n, s);
    if (osty) {
        applyStyle(s, *osty);
    }
    putStyle(out, s);
    return true;
}

bool TerminalRenderer::Impl::renderLiteral(
    std::string & out, const cmark::Node & n, const char * data, size_t sz, const Style * osty)
{
    size_t i = 0;
    while (i < sz) {
        const char * start = data + i;
        while (i < sz && data[i] != '\n')
            i++;
        size_t len = (data + i) - start;
        i++;

        if (!startLine(out, n, osty))
            return false;
        ssize_t cols = escape(out, start, len);
        if (cols < 0)
            return false;
        advance(len);
        if (!endLine(out, n, osty))
            return false;
    }
    return true;
}

bool TerminalRenderer::Impl::renderBuf(
    std::string & out, const cmark::Node & n, const char * data, size_t sz, const Style * osty)
{
    // Check if we're in a literal context
    auto nn = &n;
    while (nn) {
        auto type = cmark_node_get_type(const_cast<cmark::Node *>(nn));
        if (type == CMARK_NODE_CODE_BLOCK || type == CMARK_NODE_HTML_BLOCK) {
            return renderLiteral(out, n, data, sz, osty);
        }
        nn = cmark_node_parent(const_cast<cmark::Node *>(nn));
    }

    // Word wrapping mode
    size_t i = 0;
    bool begin = true;
    bool end = false;

    while (i < sz) {
        bool needspace = std::isspace(static_cast<unsigned char>(data[i]));
        bool hasspace = !out.empty() && std::isspace(static_cast<unsigned char>(out.back()));

        // Skip to next word
        while (i < sz && std::isspace(static_cast<unsigned char>(data[i])))
            i++;
        const char * start = data + i;
        while (i < sz && !std::isspace(static_cast<unsigned char>(data[i])))
            i++;

        size_t len = (data + i) - start;
        size_t nlen = len + (needspace ? 1 : 0);

        // Line wrapping
        if ((needspace || hasspace) && col > 0 && col + nlen >= width) {
            if (!endLine(out, n, osty))
                return false;
            end = false;
        }

        // Start new line or emit space
        if (last_blank && len) {
            if (!startLine(out, n, osty))
                return false;
            begin = false;
            end = true;
        } else if (!last_blank) {
            if (begin && len) {
                if (!startWords(out, n, osty))
                    return false;
                begin = false;
                end = true;
            }
            if (needspace) {
                out += " ";
                advance(1);
            }
        }

        // Emit the word
        ssize_t cols = escape(out, start, len);
        if (cols < 0)
            return false;
        advance(cols);
    }

    if (end) {
        assert(!begin);
        if (!endWords(out, n, osty))
            return false;
    }

    return true;
}

bool TerminalRenderer::Impl::renderHrule(std::string & out, const cmark::Node & n, const char * hr, const Style * sty)
{
    size_t sz = std::strlen(hr);
    if (sz == 0)
        return true;

    ssize_t ssz = mbsWidth(hr, sz);
    if (ssz < 0)
        return false;
    if (ssz == 0)
        return true;

    tmp.clear();
    for (size_t i = 0; i + ssz <= width; i += ssz) {
        tmp += hr;
    }

    return renderLiteral(out, n, tmp.data(), tmp.size(), sty);
}

bool TerminalRenderer::Impl::render(std::string & out, const cmark::Node & n)
{
    auto type = cmark_node_get_type(const_cast<cmark::Node *>(&n));

    // Push to stack
    stack.push_back(NodeStack{&n, 0});

    // Vertical space before
    size_t vs = 0;
    switch (type) {
    case CMARK_NODE_DOCUMENT:
        for (size_t i = 0; i < vmargin; i++)
            out += "\n";
        last_blank = -1;
        break;
    case CMARK_NODE_CODE_BLOCK:
    case CMARK_NODE_HTML_BLOCK:
    case CMARK_NODE_BLOCK_QUOTE:
    case CMARK_NODE_HEADING:
    case CMARK_NODE_LIST:
    case CMARK_NODE_PARAGRAPH:
        vs = 2;
        break;
    case CMARK_NODE_ITEM:
        vs = 1;
        break;
    case CMARK_NODE_LINEBREAK:
        vs = 1;
        break;
    case CMARK_NODE_THEMATIC_BREAK:
        vs = 2;
        break;
    case CMARK_NODE_NONE:
    case CMARK_NODE_CUSTOM_BLOCK:
    case CMARK_NODE_TEXT:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_CODE:
    case CMARK_NODE_HTML_INLINE:
    case CMARK_NODE_CUSTOM_INLINE:
    case CMARK_NODE_EMPH:
    case CMARK_NODE_STRONG:
    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
        // No vertical space needed
        break;
    }

    if (vs > 0 && !vspace(out, n, vs)) {
        stack.pop_back();
        return false;
    }

    // Handle link entry
    const cmark::Node * old_in_link = in_link;
    if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE) {
        const char * url = cmark_node_get_url(const_cast<cmark::Node *>(&n));
        if (url && !(opts.noLink || (opts.noRelLink && isRelativeLink(url)))) {
            in_link = &n;
        }
    }

    // Render children
    bool ok = true;
    auto child = cmark_node_first_child(const_cast<cmark::Node *>(&n));
    while (child && ok) {
        ok = render(out, *child);
        child = cmark_node_next(child);
    }

    if (!ok) {
        stack.pop_back();
        in_link = old_in_link;
        return false;
    }

    // Render content
    const char * literal = cmark_node_get_literal(const_cast<cmark::Node *>(&n));

    switch (type) {
    case CMARK_NODE_THEMATIC_BREAK:
        ok = renderHrule(out, n, ifx_hrule, nullptr);
        break;

    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE:
        if (literal) {
            ok = renderBuf(out, n, literal, std::strlen(literal), nullptr);
        }
        break;

    case CMARK_NODE_CODE_BLOCK:
    case CMARK_NODE_HTML_BLOCK:
    case CMARK_NODE_HTML_INLINE:
        if (literal) {
            ok = renderBuf(out, n, literal, std::strlen(literal), nullptr);
        }
        break;

    case CMARK_NODE_LINK: {
        const char * url = cmark_node_get_url(const_cast<cmark::Node *>(&n));
        if (url && !(opts.noLink || (opts.noRelLink && isRelativeLink(url)))) {
            tmp = ifx_link_sep;
            ok = renderBuf(out, n, tmp.data(), tmp.size(), nullptr);
            if (ok) {
                ok = renderBuf(out, n, url, std::strlen(url), nullptr);
            }
        }
        break;
    }

    case CMARK_NODE_IMAGE: {
        const char * url = cmark_node_get_url(const_cast<cmark::Node *>(&n));
        tmp = ifx_imgbox_left;
        ok = renderBuf(out, n, tmp.data(), tmp.size(), &sty_imgbox);

        // Note: CMark doesn't store alt text separately, so we skip rendering it
        // In lowdown, alt text is stored separately, but in CMark it's in the children

        if (ok && url && !(opts.noLink || (opts.noRelLink && isRelativeLink(url)))) {
            tmp = ifx_imgbox_sep;
            ok = renderBuf(out, n, tmp.data(), tmp.size(), &sty_imgbox);
            if (ok) {
                ok = renderBuf(out, n, url, std::strlen(url), &sty_imgurl);
            }
        }

        if (ok) {
            tmp = ifx_imgbox_right;
            ok = renderBuf(out, n, tmp.data(), tmp.size(), &sty_imgbox);
        }
        break;
    }

    case CMARK_NODE_SOFTBREAK:
        out += " ";
        break;

    case CMARK_NODE_NONE:
    case CMARK_NODE_DOCUMENT:
    case CMARK_NODE_BLOCK_QUOTE:
    case CMARK_NODE_LIST:
    case CMARK_NODE_ITEM:
    case CMARK_NODE_CUSTOM_BLOCK:
    case CMARK_NODE_PARAGRAPH:
    case CMARK_NODE_HEADING:
    case CMARK_NODE_LINEBREAK:
    case CMARK_NODE_CUSTOM_INLINE:
    case CMARK_NODE_EMPH:
    case CMARK_NODE_STRONG:
        // No content to render directly (children handle it)
        break;
    }

    // Restore link state
    in_link = old_in_link;

    // Pop stack
    stack.pop_back();

    // Handle document footer
    if (type == CMARK_NODE_DOCUMENT) {
        // Strip trailing newlines but for the vmargin
        while (!out.empty() && out.back() == '\n') {
            out.pop_back();
        }
        out += "\n";
        for (size_t i = 0; i < vmargin; i++) {
            out += "\n";
        }
    }

    return ok;
}

// ============================================================================
// Public API
// ============================================================================

TerminalRenderer::TerminalRenderer(const TerminalOptions & opts)
    : impl(std::make_unique<Impl>(opts))
{
}

TerminalRenderer::~TerminalRenderer() = default;

std::string TerminalRenderer::render(cmark::Node & root)
{
    std::string out;
    impl->stack.clear();
    impl->in_link = nullptr;
    impl->col = 0;
    impl->last_blank = -1;

    if (!impl->render(out, root)) {
        throw std::runtime_error("Failed to render terminal output");
    }

    return out;
}

std::string renderTerminal(cmark::Node & root, const TerminalOptions & opts)
{
    TerminalRenderer renderer(opts);
    return renderer.render(root);
}

} // namespace cmark
