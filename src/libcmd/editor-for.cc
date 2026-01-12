#include "nix/cmd/editor-for.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/fmt.hh"
#include "nix/util/source-path.hh"
#include "nix/util/strings.hh"
#include "nix/util/types.hh"

#include <cstdint>

namespace nix {

Strings editorFor(const SourcePath & file, uint32_t line)
{
    auto path = file.getPhysicalPath();
    if (!path)
        throw Error("cannot open '%s' in an editor because it has no physical path", file);
    auto editor = getEnv("EDITOR").value_or("cat");
    auto args = tokenizeString<Strings>(editor);
    if (line > 0
        && (editor.contains("emacs") || editor.contains("nano") || editor.contains("vim") || editor.contains("kak")))
        args.push_back(fmt("+%d", line));
    args.push_back(path->string());
    return args;
}

} // namespace nix
