# Release 1.3 (2013-01-04)

This is primarily a bug fix release. When this version is first run on
Linux, it removes any immutable bits from the Nix store and increases
the schema version of the Nix store. (The previous release removed
support for setting the immutable bit; this release clears any remaining
immutable bits to make certain operations more efficient.)

This release has contributions from Eelco Dolstra and Stuart
Pernsteiner.
