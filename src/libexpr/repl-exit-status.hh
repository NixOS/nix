#pragma once

namespace nix {

/**
 * Exit status returned from the REPL.
 */
enum class ReplExitStatus {
    /**
     * The user exited with `:quit`. The program (e.g., if the REPL was acting
     * as the debugger) should exit.
     */
    QuitAll,
    /**
     * The user exited with `:continue`. The program should continue running.
     */
    Continue,
};

}
