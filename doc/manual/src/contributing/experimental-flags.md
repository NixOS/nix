This section describes the notion of a Nix's "experimental
features", and how it fits into the big picture of the development of Nix.

# What are these xp features?

Since Nix 2.4, some features of Nix can be flagged as
experimental. This means that they can be changed or removed at any time.
Users must explicitly opt in to them by adding the name of the experimental feature to
the `experimental-features` configuration option. This is really handy
as it makes it harder to accidentally use an experimental feature
without knowing it.

# When should an experimental feature be used

A change in the Nix codebase should be guarded by an experimental feature flag
if it is likely to either introduce a regression of any kind, or if it
implies a non-trivial change in the external interface of Nix (be it the language, the cli or
anything else that's considered as "public").

For example, these changes need to be guarded by an experimental flag:

-   Any language change (new builtin, syntactic or semantic change,
    etc..),

-   Any change to the (non-experimental) CLI interface

-   A non-trivial internal refactoring that's likely to introduce some
    bugs\[\^1\]

\[\^1\]: Having to be weighted against the costs of course. If the
refactoring changes a low-level interface and keeping both versions
around essentially means duplicating half of the codebase, then it might
not be worth doing that.

# Lifecycle of an experimental feature

Experimental features being, well, experimental, they all have to be
treated on a case-by-case basis.However, the standard workflow for an
experimental feature is as follows:

-   The feature is merged, under an experimental flag (possibly over a
    long period of time)

-   It becomes part of a release

-   After at least a full 6w release cycle, the feature becomes enabled
    by default

-   After at lesat another full 6w release cycle, the old behavior is
    removed

This is obviously just indicative. In particular the third step is only
useful if the feature is susceptible to introduce some significant and
hard to work-around regression (for example if it requires some non-trivial changes in existing codepaths). Conversely, the second is useless if people have no
compelling reason to switch the flag (for example a purely internal
refactoring)

# Relation to the RFC process

Experimental features and RFCs attack a similar problem in that they both exist to
make it possible to carry-out substantial changes while minimizing the
risk.However − as far as Nix is concerned at least − they attack it
under a different (and complementary) angle:

-   The goal of an RFC is to explicit all the **external** implications
    of a feature: Explain why it is wanted, which new use-cases it
    enable, which interface changes it require, etc..It is primarily a
    *design* and *communication* issue, targeted towards the community
    in general (and not specifically the Nix developpers).

-   The goal of an experimental feature is to make it possible new and
    possibly complex developments without requiring a (costly)
    long-running fork but also without sacrificing the stability of the
    main branch.It is primarily an *implementation* issue, targeted
    towards the Nix developpers and early testers.

In practice, this means that having an experimental feature and having an
RFC are two separate concerns. It can be legitimate to have a
development guarded by an experimental flag but without an RFC (for
example a non-trivial refactoring that doesn't change the interface but
might introduce some bugs because of the code churn). Conversely it is
possible (though probably less likely) to have an RFC that yields a
change in the Nix codebase but without requirering an experimental feature.
Or an RFC that yields several experimental features because the end-goal
described by the RFC has several parts that can be implemented and used
independently.
