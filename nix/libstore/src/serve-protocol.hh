#pragma once
///@file

namespace nix {

#define SERVE_MAGIC_1 0x390c9deb
#define SERVE_MAGIC_2 0x5452eecb

#define SERVE_PROTOCOL_VERSION (2 << 8 | 7)
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)

/**
 * The "serve protocol", used by ssh:// stores.
 *
 * This `struct` is basically just a `namespace`; We use a type rather
 * than a namespace just so we can use it as a template argument.
 */
struct ServeProto
{
    /**
     * Enumeration of all the request types for the protocol.
     */
    enum struct Command : uint64_t;
};

enum struct ServeProto::Command : uint64_t
{
    QueryValidPaths = 1,
    QueryPathInfos = 2,
    DumpStorePath = 3,
    ImportPaths = 4,
    ExportPaths = 5,
    BuildPaths = 6,
    QueryClosure = 7,
    BuildDerivation = 8,
    AddToStoreNar = 9,
};

/**
 * Convenience for sending operation codes.
 *
 * @todo Switch to using `ServeProto::Serialize` instead probably. But
 * this was not done at this time so there would be less churn.
 */
inline Sink & operator << (Sink & sink, ServeProto::Command op)
{
    return sink << (uint64_t) op;
}

/**
 * Convenience for debugging.
 *
 * @todo Perhaps render known opcodes more nicely.
 */
inline std::ostream & operator << (std::ostream & s, ServeProto::Command op)
{
    return s << (uint64_t) op;
}

}
