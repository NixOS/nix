# A logical path that can hold a Windows root

Status: design proposal with a proof of concept. Nothing here is wired into
the evaluator.

## Why this exists

`CanonPath` is the type every abstract path in Nix passes through, and it
cannot represent a Windows root at all. Its own doc comment says so
plainly: paths "are always Unix-style paths, regardless of what OS Nix is
running on."

That is not an oversight, it is load-bearing. `canon-path.cc:16`
canonicalises through `canonPathInner<UnixPathTrait>`, and
`UnixPathTrait::rootNameLen` returns 0 unconditionally. The generic
algorithm in `file-path-impl.hh` *does* handle a root name -- it copies it
verbatim before walking components -- so the machinery exists and only the
Unix trait declines to use it.

The consequence is concrete. `\\server\share` handed to `CanonPath`
becomes one relative component, because `\` is not a separator to
`UnixPathTrait` and `\\server\share` contains no `/` at all. Nothing
errors; the path is silently wrong.

Fixing `WindowsPathTrait::rootNameLen` does not reach this. That trait
governs *native* paths, and the two are deliberately separate layers.

## What is proposed

A `LogicalPath` (`src/libutil/include/nix/util/logical-path.hh`) that
separates the two things `CanonPath` conflates: what a path is anchored
to, and the components below it.

```c++
using Root = std::variant<Posix, Drive, Unc, Device>;

Root root_;
std::vector<std::string> components_;
```

Invariants, established in the constructor and preserved by every mutator:

- no component is empty, `.`, or `..`
- no component contains a NUL byte or a `/`
- components are WTF-8, so a name that is not well-formed UTF-16 survives
- a `Drive` letter is upper case and a `Unc` host is lower case

The external form is a `file` URI per RFC 8089. That RFC is the only
standard that describes how a Windows root maps onto a hierarchical name,
and Nix already cites it twice -- `url.hh:82` and a `TODO` at
`url.cc:682` -- so this continues an existing direction rather than
starting a new one.

## Root mapping

| Native | Root | URI |
| --- | --- | --- |
| `/usr/lib` | `Posix` | `file:///usr/lib` |
| `C:\foo` | `Drive{'C'}` | `file:///C:/foo` |
| `c:\foo` | `Drive{'C'}` | `file:///C:/foo` |
| `\\host\share\a` | `Unc{host,share}` | `file://host/share/a` |
| `\\?\C:\foo` | `Drive{'C'}` | `file:///C:/foo` |
| `\\?\UNC\h\s\f` | `Unc{h,s}` | `file://h/s/f` |
| `\\.\PhysicalDrive0` | `Device{...}` | `nix-win32-device:///PhysicalDrive0` |

The UNC row is RFC 8089 appendix E.3.1: host to authority, share and
object names to path segments.

**The extended-length prefixes are normalised away.** `\\?\C:` names the
same volume as `C:`, and `\\?\UNC\h\s` the same share as `\\h\s`, so
collapsing them keeps the type from having two spellings for one path.
What that discards is discussed under non-goals.

**Only a true device path needs the extension.** RFC 8089 states it "does
not define a mechanism" for the Win32 File Namespace, so `Device` renders
under `nix-win32-device:` and can never be mistaken for a conformant
`file` URI. This matters less than it sounds: `\\.\PhysicalDrive0` is not
a filesystem path, and no Nix store lives there.

## Where WTF-8 and percent-encoding meet

The order is: UTF-16 → WTF-8 → percent-encoded octets.

- Components are held as WTF-8 bytes, using `wtf8FromUtf16` /
  `utf16FromWtf8` from `wtf8.hh`. Both directions are total.
- Rendering percent-encodes each component's bytes with
  `percentEncode(s, ":@")`, so `/`, `%`, `#`, `?` and space are all
  escaped and `:` survives to let a drive render as `C:` rather than
  `C%3A`.
- Parsing percent-decodes each segment back to WTF-8 bytes.

**This is a Nix extension, not RFC conformance, and the distinction should
not be blurred.** RFC 8089 section 4 says UTF-8 SHOULD be applied before
percent-encoding and then states that "a decision not to use
percent-encoded UTF-8 is outside the scope of this specification." An
unpaired surrogate has no UTF-8 encoding, so carrying one means leaving
that SHOULD behind.

What is preserved is the *syntax*: percent-encoded WTF-8 is still a
well-formed RFC 3986 URI, because RFC 3986 percent-encoding is defined
over octets and does not care what they mean. So a conformant parser will
accept and round-trip these URIs; it simply will not be able to display
the offending component as text. Claiming more than that would be wrong.

## Comparison and ordering

Equality is structural over the root and the component vector. Case
folding at construction is what makes it correct:

- **Drive letters fold to upper case.** RFC 8089 appendix E.2 states
  drive-letter comparison is case-insensitive and that some usages treat
  URIs differing only in drive-letter case as identical. Folding at
  construction gets this without a custom comparator, so `operator==`,
  `operator<=>` and hashing all agree for free.
- **UNC hosts fold to lower case**, because RFC 3986 section 3.2.2 makes
  the authority case-insensitive.
- **Shares do not fold.** RFC 3986 leaves the path case-sensitive. Windows
  itself compares shares case-insensitively, so this is a deliberate
  divergence: folding would make two distinct URIs compare equal, and the
  URI is the thing being modelled. A caller that needs Windows semantics
  has to ask for them.

Ordering compares the root first, then components pairwise. That
reproduces the property `CanonPath::operator<=>` documents -- a directory
sorts immediately before its children -- which byte-wise comparison of a
joined string does *not* have, because `!` is greater than `/` and so
`/foo!` would sort between `/foo` and `/foo/bar`. Comparing the root first
also stops two volumes from interleaving.

`..` resolves against components and stops at the root. Letting it escape
would resolve out of a share into a different volume, which is the same
reason `WindowsPathTrait::uncRootEnd` treats a server-with-no-share as
entirely root name.

## The upstream blocker

**`url.cc:184` rejects any `file` URL with a non-empty host, so the one
mapping RFC 8089 gives for a UNC path cannot be parsed by Nix today.**

```c++
auto transportIsFile = parseUrlScheme(scheme).transport == "file";
if (authority && authority->host.size() && transportIsFile)
    throw BadURL("file:// URL '%s' has unexpected authority '%s'", ...);
```

This was found by execution, not by reading: three round-trip tests failed
with `file:// URL 'file://host/share/a/b' has unexpected authority 'host'`
before the proof of concept stopped calling `parseURL`.

The comment above that guard cites RFC 3986 section 3.2.2, which says that
for `file`, "no authority, an empty host, and `localhost` all mean the
end-user's machine." That is true, and it does not say other hosts are
invalid -- it says what those three *mean*. RFC 8089 appendix E.3.1 then
defines the authority slot as where a UNC host goes. So the guard is
stricter than either RFC requires.

**Relaxing it is a maintainer decision, not a detail.** Two tests assert
the current behaviour:

- `src/libutil-tests/url.cc:390` -- `fixGitURL("file://var/repos/x")` must
  throw
- `src/libutil-tests/url.cc:676` -- `parseURL("file://www.example.org/video.mp4")`
  must throw

The second is the real question. `file://www.example.org/video.mp4` is
almost certainly a user mistake for `https://`, and rejecting it is a
kindness. Under E.3.1 it is instead a valid reference to
`\\www.example.org\video.mp4`. Those two readings cannot both be the
default. Options, with what each costs:

1. **Permit a non-empty host for `file`.** One condition removed; both
   tests above have to be rewritten. Users lose a helpful error on a
   likely typo.
2. **Permit it only when a Windows-paths feature is enabled.** Keeps the
   error for everyone else, at the cost of a parser whose accepted
   language depends on configuration -- which tends to produce bugs that
   only appear on one platform.
3. **Keep `parseURL` as it is and give logical paths their own parser.**
   What the proof of concept does. No behaviour change anywhere, at the
   cost of two parsers for one syntax, which will drift.

This proposal does not choose. `LogicalPath::parseUri` currently does its
own scheme/authority split, still using `percentEncode`/`percentDecode`
from `url.hh`, and a test
(`logicalPath.nixCannotYetParseTheRfc8089UncForm`) asserts the current
rejection so that lifting it fails loudly and points here.

## Blast radius

Measured on `origin/master` at `72385de1b`:

| Measure | Count |
| --- | --- |
| `CanonPath` occurrences | 949 |
| files mentioning it | 102 |
| constructor calls | 303 |
| `CanonPath::root` uses | 97 |
| `.abs()` calls | 71 |
| `.rel()` calls | 57 |

By component: libutil 29 files, libstore 15, libfetchers 15, `nix` 10,
libutil-tests 9, libexpr 7, libexpr-tests 5, libflake 3, then single
digits elsewhere.

The split that matters is not by file but by what a call site does with
the string:

- **Mechanical.** The 303 constructor calls and most component
  walking. A `LogicalPath` built from a `Posix` root behaves like today's
  `CanonPath`, so these change type and not meaning.
- **Semantic.** The 71 `.abs()` and 57 `.rel()` calls, because both hand
  out a flat `std::string` and a rooted path has no single flat form worth
  handing out. Every one has to be looked at: some want a display string,
  some want a key, some want something to concatenate. These are the real
  cost, and they are why this is a proposal rather than a patch.

## What was rejected

**Storing the URI string as the holder.** Tempting, since the URI is
already the serialisation, and it would make the type trivially
round-trippable. Rejected because it destroys the invariants: any
`std::string` is then a candidate value, validity can only be checked by
re-parsing, and every component access costs a parse. `CanonPath`'s value
is that an instance is *known* canonical; a string-holding type gives that
up to save a struct.

**A flat string with the root encoded into it.** The minimal change:
teach `CanonPath` that a leading `/C:/` or `//host/share/` is a root and
keep everything else. The diff would be small and most of the 949 sites
would not move. Rejected because it reintroduces exactly the ambiguity the
root exists to remove -- `/host/share` and `//host/share` differ by one
character and mean different volumes -- and because `.abs()` would keep
returning a string whose first segments sometimes are and sometimes are
not a root, which is the bug being fixed, relocated.

**A variant of roots with no URI story.** This is half of what is
proposed, and on its own it is defensible: the in-memory shape is what
fixes the correctness problem, and the serialisation is a separate
question. Rejected as insufficient rather than wrong, because logical
paths already get written into lock files, store paths, error messages and
the C API, and inventing an ad-hoc textual form for those is how you end
up with several. RFC 8089 supplies one that is already cited in this
codebase.

## Non-goals

- **Converting the evaluator.** Not attempted. The 128 semantic call sites
  above are a separate piece of work.
- **Preserving `\\?\` no-normalisation semantics.** The extended-length
  prefix tells Win32 *not* to normalise a path, so `\\?\C:\a\..\b` is a
  literal name rather than `C:\b`. A type whose purpose is canonicalisation
  cannot preserve that, and this one does not: it normalises `\\?\C:` to
  `Drive{'C'}` and resolves `..`. A caller needing the literal behaviour
  must stay at the native layer.
- **Drive-relative paths.** `C:foo` means "foo, relative to the working
  directory *on* drive C", which is per-drive mutable process state. The
  proof of concept does not distinguish it from `C:\foo`. A real
  implementation must either represent it or reject it; silently treating
  it as absolute is wrong.
- **Windows share case semantics.** See the comparison section.
- **Making `CanonPath` and `LogicalPath` interconvertible in general.**
  Only a `Posix`-rooted `LogicalPath` has a `CanonPath` equivalent.

## Known limitations of the proof of concept

- A relative path parses to a `Posix` root, so `a/b` and `/a/b` are
  indistinguishable. `CanonPath` has the same behaviour by design -- it
  documents that it "does not need an absolute/relative distinction" --
  but a type that models URIs should probably not inherit it.
- `parseUri` does its own scheme/authority split rather than using
  `parseURL`, for the reason above. Two parsers for one syntax is a defect
  that should not survive into production.
- Nothing is tested on Windows. Everything below ran natively on Linux,
  which was a deliberate design choice: the WTF-8 conversion lives in a
  portable `libutil/wtf8.cc` and `LogicalPath` is platform-independent, so
  both are testable everywhere. No Wine run was performed, and no claim is
  made about executed Windows behaviour.

## Verification

Built and run natively on Linux against `origin/master@72385de1b`.
`src/libutil` configures as a standalone meson subproject, which is how
this was built without a working `nix develop` on the host.

- `libutil` compiles clean, 78 targets, zero errors.
- `logical-path` tests: **32 tests, 32 passed, 0 failed.**

Each claim was shown able to fail, by neutering the implementation and
confirming the expected tests went red:

| Neutered | Red |
| --- | --- |
| percent-encoding removed | 3: reserved characters, literal `%`, unpaired surrogate |
| constructor case folding removed | **0** -- see below |
| constructor case folding, after adding a direct test | 1: `theConstructorFoldsCaseToo` |
| `..` resolution removed | 1: `dotAndDotDotAreResolved` |

The zero is the useful row. The two case-folding tests went through
`parseNative`, which folds in `parseNativeRoot` before the constructor
ever sees the value, so the constructor's folding was untested and
removing it changed nothing. The constructor is public, so it does need to
fold; a test that builds a value directly was added, and the neuter then
produced the expected red. Without that round the redundancy would have
shipped as covered.

One more measurement worth recording: an early run reported
`EXIT=0` while three tests were failing, because the command piped
through `tail` and the shell reported `tail`'s status. Read the gtest
totals, never the exit code.
