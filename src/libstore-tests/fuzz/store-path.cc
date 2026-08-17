#include "nix/store/path.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;

    auto view = std::string_view(reinterpret_cast<const char *>(data), size);

    std::optional<StorePath> parsed = [view]() -> std::optional<StorePath> {
        try {
            return StorePath(view);
        } catch (const BadStorePath &) {
            /* This is the only type of exception it can throw. */
            return std::nullopt;
        }
    }();

    if (!parsed)
        /* Keep in the corpus. */
        return 0;

    auto parsedView = parsed->to_string();

    /* Now actually check invariants. See nameRegexStr. MaxPathLen is actually
       a misnomer - it's the maximum length of the store object name, not the
       pathname. Pathname length includes the hash len + `-`.
       Is the following checks a bit overkill? Maybe - but StorePath is also used
       everywhere so we better get it right. */
    assert(parsedView.size() <= StorePath::MaxPathLen + StorePath::HashLen + 1);

    /* Name can't be empty, so strict comparison. */
    assert(parsedView.size() > StorePath::HashLen + 1);

    /* Some trivial things to check. */
    assert(parsed->hashPart().data() == parsedView.data());
    assert(parsed->hashPart().size() == StorePath::HashLen);

    /* Check that the hash part is valid nix32. */
    for (char c : parsed->hashPart()) {
        switch (c) {
        case 'a' ... 'z':
            if (c == 'e' || c == 'o' || c == 'u' || c == 't')
                __builtin_trap();
            break;
        case '0' ... '9':
            break;
        default:
            __builtin_trap();
        }
    }

    /* Must be followed by a dash *always*. Duh. */
    assert(parsedView[StorePath::HashLen] == '-');

    auto name = parsed->name();

    assert(name.data() == parsedView.data() + StorePath::HashLen + 1);
    assert(name.size() == parsedView.size() - (StorePath::HashLen + 1));

    assert(name != ".");
    assert(name != "..");
    assert(!name.starts_with(".-"));
    assert(!name.starts_with("..-"));

    for (char c : name) {
        switch (c) {
        case 'a' ... 'z':
        case 'A' ... 'Z':
        case '0' ... '9':
        case '+':
        case '-':
        case '.':
        case '_':
        case '?':
        case '=':
            break;
        default:
            __builtin_trap();
        }
    }

    return 0;
}
