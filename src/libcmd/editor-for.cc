#include "nix/cmd/editor-for.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/source-path.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"

namespace nix {

std::tuple<OsStrings, AutoCloseFD, AutoDelete> editorFor(const SourcePath & file, uint32_t line, bool readOnly)
{
    auto path = file.getPhysicalPath();
    OsString editor = getEnvOsNonEmpty(OS_STR("EDITOR")).value_or(OS_STR("cat"));
    auto args = tokenizeString<OsStrings>(editor);
    if (line > 0
        && (editor.contains(OS_STR("emacs")) || editor.contains(OS_STR("nano")) || editor.contains(OS_STR("vim"))
            || editor.contains(OS_STR("kak"))))
        args.push_back(string_to_os_string(fmt("+%d", line)));

    if (path) {
        args.push_back(path->native());
        return {std::move(args), AutoCloseFD{}, AutoDelete{}};
    }

    /* Resolve symlinks when creating a temporary. That's how a regular editor
       would behave. Also GitSourceAccessor behaves poorly with symlinks in the
       path and fails with "«...» does not exist". */
    auto file2 = file.resolveSymlinks();
    auto stat = file2.lstat();
    /* TODO: Maybe we should print a directory listing and open that instead? */
    if (stat.type != SourceAccessor::tRegular)
        throw Error("can't open a file %s of type '%s'", file2.to_string(), stat.typeString());

    auto tempDir = createTempDir(defaultTempDir(), "nix-edit", 0700);
    AutoDelete autoDel(tempDir, /*recursive=*/true);
    auto tempPath = tempDir / file2.path.baseName().value_or("nix-edit");

    /* Create the file with the same name, so editors that recognise file
       extensions spin up syntax highlighting and LSPs. */
    auto tempFd = openNewFileForWrite(
        tempPath,
        readOnly ? 0400 : 0600,
        {.truncateExisting = false, .followSymlinksOnTruncate = false, .writeOnly = false});

    if (!tempFd)
        throw NativeSysError("failed to create temporary file %s", PathFmt(tempPath));

    /* Copy the contents into the created copy. */
    FdSink fileSink(tempFd.get());
    file2.readFile(fileSink);
    fileSink.flush();
    args.push_back(tempPath);

    return {std::move(args), std::move(tempFd), std::move(autoDel)};
}

} // namespace nix
