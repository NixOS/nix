#pragma once

namespace nix {


#define SERVE_MAGIC_1 0x390c9deb
#define SERVE_MAGIC_2 0x5452eecb

#define SERVE_PROTOCOL_VERSION 0x101
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


typedef enum {
    cmdQuery = 0,
    cmdSubstitute = 1,
} ServeCommand;

typedef enum {
    qCmdHave = 0,
    qCmdInfo = 1,
} QueryCommand;

}
