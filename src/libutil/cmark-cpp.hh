#pragma once
///@file

#include "types.hh"
#include "util.hh"

#include <cmark.h>

namespace nix::cmark {

using Node = struct cmark_node;
using NodeType = cmark_node_type;
using ListType = cmark_list_type;

using Iter = struct cmark_iter;

struct Deleter
{
    void operator () (Node * ptr) { cmark_node_free(ptr); }
    void operator () (Iter * ptr) { cmark_iter_free(ptr); }
};

template <typename T>
using UniquePtr = std::unique_ptr<Node, Deleter>;

static inline void parse_document(Node & root, std::string_view s, int options)
{
    cmark_parser * parser = cmark_parser_new_with_mem_into_root(
        options,
        cmark_get_default_mem_allocator(),
        &root);
    cmark_parser_feed(parser, s.data(), s.size());
    (void) cmark_parser_finish(parser);
    cmark_parser_free(parser);
}

static inline UniquePtr<Node> parse_document(std::string_view s, int options)
{
    return UniquePtr<Node> {
        cmark_parse_document(s.data(), s.size(), options)
    };
}

static inline std::unique_ptr<char, FreeDeleter> render_commonmark(Node & root, int options, int width)
{
    return std::unique_ptr<char, FreeDeleter> {
        cmark_render_commonmark(&root, options, width)
    };
}

static inline std::unique_ptr<char, FreeDeleter> render_xml(Node & root, int options)
{
    return std::unique_ptr<char, FreeDeleter> {
        cmark_render_xml(&root, options)
    };
}

static inline UniquePtr<Node> node_new(NodeType type)
{
    return UniquePtr<Node> {
        cmark_node_new(type)
    };
}

/**
 * The parent takes ownership
 */
static inline Node & node_append_child(Node & node, UniquePtr<Node> child)
{
    auto status = (bool) cmark_node_append_child(&node, &*child);
    assert(status);
    return *child.release();
}

static inline bool node_set_literal(Node & node, const char * content)
{
    return (bool) cmark_node_set_literal(&node, content);
}

static inline bool node_set_list_type(Node & node, ListType type)
{
    return (bool) cmark_node_set_list_type(&node, type);
}

}
