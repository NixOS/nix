#include "nix/util/util.hh"
#include "nix/cmd/editor-for.hh"

namespace nix {

Strings editorFor(const Path & file, uint32_t line)
{
    auto editor = getEnv("EDITOR").value_or("cat");
    auto args = tokenizeString<Strings>(editor);
    if (line > 0 && (
        editor.find("emacs") != std::string::npos ||
        editor.find("nano") != std::string::npos ||
        editor.find("vim") != std::string::npos ||
        editor.find("kak") != std::string::npos))
        args.push_back(fmt("+%d", line));
    args.push_back(file);
    return args;
}

}
