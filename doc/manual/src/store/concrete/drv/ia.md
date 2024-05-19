# Input-Addressing

Input addressing means that the digest derives from how the store object was produced, namely its build inputs and build plan.

To compute the hash of a store object one needs a deterministic serialisation, i.e., a binary string representation which only changes if the store object changes.

Nix has a custom serialisation format called Nix Archive (NAR)

Store object references of this sort can *not* be validated from the content of the store object.
Rather, a cryptographic signature has to be used to indicate that someone is vouching for the store object really being produced from a build plan with that digest.
