R""(

# Examples

* Get the credentials for a host:

  ```console
  # printf 'protocol=https\nhost=cache.example.org\n' | nix auth fill
  protocol=https
  host=cache.example.org
  username=alice
  password=foobar
  ```

# Description

Read a [`git-credential`](https://git-scm.com/docs/git-credential)
request from standard input, resolve it against
[`auth-sources`](@docroot@/command-ref/conf-file.md#conf-auth-sources),
and write the resulting fields to standard output.

With `--require`, prompt via `$SSH_ASKPASS` for any field no source
provides.

)""
