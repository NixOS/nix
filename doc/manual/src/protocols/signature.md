# Signature

In Nix, signatures are used to trust store paths using asymmetric cryptography,
in this instance: Curve25519-based cryptography.

## Store path signature format

Store path signatures are ED25519 signatures, there are two types of
signatures:

- `ValidPathInfo` will assemble a fingerprint in the form of `1;<store
  path>;<NAR hash in base32 including its own type>;<NAR size in bytes>;<NAR
  references separated by ,>` and sign this information with the ED25519
  private key.
- `Realisation` will assemble a fingerprint in the form of a JSON string: `{
  id, outPath, dependentRealisations }` and sign this information with the
  ED25519 private key.

# Remote signature protocol

The remote signature protocol is a mechanism to offload the signature of your
store paths to another machine that can possess the secret key material in a
secure way.

In this setup, Nix will contact a remote signing URL that you specified and ask
to sign fingerprints over the wire.

The protocol expects a UNIX domain socket to force you to handle proper
authentication and authorization. `socat` is a great tool to manipulate all
sorts of sockets.

## Semantics of the APIs

- `POST /sign`: expects a fingerprint as input and will return the signature
  associated to that fingerprint.
- `POST /sign-store-path`: expects a store path as a parameter and will attempt
  to sign that specific store path which is expected to be present on the
  signer's machine and return the signature in the response.
- `GET /publickey`: receives the public key as a string in the response.

## A note on the security of that scheme

You are responsible to ensure that `/sign` cannot be abused to sign anything
and everything, for this, a simple setup could involve setting up a TCP service
that requires authentication, e.g. SSH or something on the HTTP level and you
can run a highly privileged daemon on the machine that wants to benefit from
signatures presenting a UNIX domain socket to Nix.
