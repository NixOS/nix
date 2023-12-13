# Running tests

## Coverage analysis

A [coverage analysis report] is available online
You can build it yourself:

[coverage analysis report]: https://hydra.nixos.org/job/nix/master/coverage/latest/download-by-type/report/coverage

```
# nix build .#hydraJobs.coverage
# xdg-open ./result/coverage/index.html
```

[Extensive records of build metrics](https://hydra.nixos.org/job/nix/master/coverage#tabs-charts), such as test coverage over time, are also available online.

## Unit-tests

The unit tests are defined using the [googletest] and [rapidcheck] frameworks.

[googletest]: https://google.github.io/googletest/
[rapidcheck]: https://github.com/emil-e/rapidcheck
[property testing]: https://en.wikipedia.org/wiki/Property_testing

### Source and header layout

> An example of some files, demonstrating much of what is described below
>
> ```
> src
> ├── libexpr
> │   ├── local.mk
> │   ├── value/context.hh
> │   ├── value/context.cc
> │   …
> │
> ├── tests
> │   │
> │   …
> │   └── unit
> │       ├── libutil
> │       │   ├── local.mk
> │       │   …
> │       │   └── data
> │       │       ├── git/tree.txt
> │       │       …
> │       │
> │       ├── libexpr-support
> │       │   ├── local.mk
> │       │   └── tests
> │       │       ├── value/context.hh
> │       │       ├── value/context.cc
> │       │       …
> │       │
> │       ├── libexpr
> │       …   ├── local.mk
> │           ├── value/context.cc
> │           …
> …
> ```

The tests for each Nix library (`libnixexpr`, `libnixstore`, etc..) live inside a directory `tests/unit/${library_name_without-nix}`.
Given a interface (header) and implementation pair in the original library, say, `src/libexpr/value/context.{hh,cc}`, we write tests for it in `tests/unit/libexpr/tests/value/context.cc`, and (possibly) declare/define additional interfaces for testing purposes in `tests/unit/libexpr-support/tests/value/context.{hh,cc}`.

Data for unit tests is stored in a `data` subdir of the directory for each unit test executable.
For example, `libnixstore` code is in `src/libstore`, and its test data is in `tests/unit/libstore/data`.
The path to the `tests/unit/data` directory is passed to the unit test executable with the environment variable `_NIX_TEST_UNIT_DATA`.
Note that each executable only gets the data for its tests.

The unit test libraries are in `tests/unit/${library_name_without-nix}-lib`.
All headers are in a `tests` subdirectory so they are included with `#include "tests/"`.

The use of all these separate directories for the unit tests might seem inconvenient, as for example the tests are not "right next to" the part of the code they are testing.
But organizing the tests this way has one big benefit:
there is no risk of any build-system wildcards for the library accidentally picking up test code that should not built and installed as part of the library.

### Running tests

You can run the whole testsuite with `make check`, or the tests for a specific component with `make libfoo-tests_RUN`.
Finer-grained filtering is also possible using the [--gtest_filter](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests) command-line option, or the `GTEST_FILTER` environment variable.

### Characterisation testing { #characaterisation-testing-unit }

See [functional characterisation testing](#characterisation-testing-functional) for a broader discussion of characterisation testing.

Like with the functional characterisation, `_NIX_TEST_ACCEPT=1` is also used.
For example:
```shell-session
$ _NIX_TEST_ACCEPT=1 make libstore-tests_RUN
...
[  SKIPPED ] WorkerProtoTest.string_read
[  SKIPPED ] WorkerProtoTest.string_write
[  SKIPPED ] WorkerProtoTest.storePath_read
[  SKIPPED ] WorkerProtoTest.storePath_write
...
```
will regenerate the "golden master" expected result for the `libnixstore` characterisation tests.
The characterisation tests will mark themselves "skipped" since they regenerated the expected result instead of actually testing anything.

### Unit test support libraries

There are headers and code which are not just used to test the library in question, but also downstream libraries.
For example, we do [property testing] with the [rapidcheck] library.
This requires writing `Arbitrary` "instances", which are used to describe how to generate values of a given type for the sake of running property tests.
Because types contain other types, `Arbitrary` "instances" for some type are not just useful for testing that type, but also any other type that contains it.
Downstream types frequently contain upstream types, so it is very important that we share arbitrary instances so that downstream libraries' property tests can also use them.

It is important that these testing libraries don't contain any actual tests themselves.
On some platforms they would be run as part of every test executable that uses them, which is redundant.
On other platforms they wouldn't be run at all.

## Functional tests

The functional tests reside under the `tests/functional` directory and are listed in `tests/functional/local.mk`.
Each test is a bash script.

### Running the whole test suite

The whole test suite can be run with:

```shell-session
$ make install && make installcheck
ran test tests/functional/foo.sh... [PASS]
ran test tests/functional/bar.sh... [PASS]
...
```

### Grouping tests

Sometimes it is useful to group related tests so they can be easily run together without running the entire test suite.
Each test group is in a subdirectory of `tests`.
For example, `tests/functional/ca/local.mk` defines a `ca` test group for content-addressed derivation outputs.

That test group can be run like this:

```shell-session
$ make ca.test-group -j50
ran test tests/functional/ca/nix-run.sh... [PASS]
ran test tests/functional/ca/import-derivation.sh... [PASS]
...
```

The test group is defined in Make like this:
```makefile
$(test-group-name)-tests := \
  $(d)/test0.sh \
  $(d)/test1.sh \
  ...

install-tests-groups += $(test-group-name)
```

### Running individual tests

Individual tests can be run with `make`:

```shell-session
$ make tests/functional/${testName}.sh.test
ran test tests/functional/${testName}.sh... [PASS]
```

or without `make`:

```shell-session
$ ./mk/run-test.sh tests/functional/${testName}.sh tests/functional/init.sh
ran test tests/functional/${testName}.sh... [PASS]
```

To see the complete output, one can also run:

```shell-session
$ ./mk/debug-test.sh tests/functional/${testName}.sh tests/functional/init.sh
+(${testName}.sh:1) foo
output from foo
+(${testName}.sh:2) bar
output from bar
...
```

The test script will then be traced with `set -x` and the output displayed as it happens, regardless of whether the test succeeds or fails.

### Debugging failing functional tests

When a functional test fails, it usually does so somewhere in the middle of the script.

To figure out what's wrong, it is convenient to run the test regularly up to the failing `nix` command, and then run that command with a debugger like GDB.

For example, if the script looks like:

```bash
foo
nix blah blub
bar
```
edit it like so:

```diff
 foo
-nix blah blub
+gdb --args nix blah blub
 bar
```

Then, running the test with `./mk/debug-test.sh` will drop you into GDB once the script reaches that point:

```shell-session
$ ./mk/debug-test.sh tests/functional/${testName}.sh tests/functional/init.sh
...
+ gdb blash blub
GNU gdb (GDB) 12.1
...
(gdb)
```

One can debug the Nix invocation in all the usual ways.
For example, enter `run` to start the Nix invocation.

### Troubleshooting

Sometimes running tests in the development shell may leave artefacts in the local repository.
To remove any traces of that:

```console
git clean -x --force tests
```

### Characterisation testing { #characterisation-testing-functional }

Occasionally, Nix utilizes a technique called [Characterisation Testing](https://en.wikipedia.org/wiki/Characterization_test) as part of the functional tests.
This technique is to include the exact output/behavior of a former version of Nix in a test in order to check that Nix continues to produce the same behavior going forward.

For example, this technique is used for the language tests, to check both the printed final value if evaluation was successful, and any errors and warnings encountered.

It is frequently useful to regenerate the expected output.
To do that, rerun the failed test(s) with `_NIX_TEST_ACCEPT=1`.
For example:
```bash
_NIX_TEST_ACCEPT=1 make tests/functional/lang.sh.test
```
This convention is shared with the [characterisation unit tests](#characterisation-testing-unit) too.

An interesting situation to document is the case when these tests are "overfitted".
The language tests are, again, an example of this.
The expected successful output of evaluation is supposed to be highly stable – we do not intend to make breaking changes to (the stable parts of) the Nix language.
However, the errors and warnings during evaluation (successful or not) are not stable in this way.
We are free to change how they are displayed at any time.

It may be surprising that we would test non-normative behavior like diagnostic outputs.
Diagnostic outputs are indeed not a stable interface, but they still are important to users.
By recording the expected output, the test suite guards against accidental changes, and ensure the *result* (not just the code that implements it) of the diagnostic code paths are under code review.
Regressions are caught, and improvements always show up in code review.

To ensure that characterisation testing doesn't make it harder to intentionally change these interfaces, there always must be an easy way to regenerate the expected output, as we do with `_NIX_TEST_ACCEPT=1`.

## Integration tests

The integration tests are defined in the Nix flake under the `hydraJobs.tests` attribute.
These tests include everything that needs to interact with external services or run Nix in a non-trivial distributed setup.
Because these tests are expensive and require more than what the standard github-actions setup provides, they only run on the master branch (on <https://hydra.nixos.org/jobset/nix/master>).

You can run them manually with `nix build .#hydraJobs.tests.{testName}` or `nix-build -A hydraJobs.tests.{testName}`

## Installer tests

After a one-time setup, the Nix repository's GitHub Actions continuous integration (CI) workflow can test the installer each time you push to a branch.

Creating a Cachix cache for your installer tests and adding its authorisation token to GitHub enables [two installer-specific jobs in the CI workflow](https://github.com/NixOS/nix/blob/88a45d6149c0e304f6eb2efcc2d7a4d0d569f8af/.github/workflows/ci.yml#L50-L91):

- The `installer` job generates installers for the platforms below and uploads them to your Cachix cache:
  - `x86_64-linux`
  - `armv6l-linux`
  - `armv7l-linux`
  - `x86_64-darwin`

- The `installer_test` job (which runs on `ubuntu-latest` and `macos-latest`) will try to install Nix with the cached installer and run a trivial Nix command.

### One-time setup

1. Have a GitHub account with a fork of the [Nix repository](https://github.com/NixOS/nix).
2. At cachix.org:
    - Create or log in to an account.
    - Create a Cachix cache using the format `<github-username>-nix-install-tests`.
    - Navigate to the new cache > Settings > Auth Tokens.
    - Generate a new Cachix auth token and copy the generated value.
3. At github.com:
    - Navigate to your Nix fork > Settings > Secrets > Actions > New repository secret.
    - Name the secret `CACHIX_AUTH_TOKEN`.
    - Paste the copied value of the Cachix cache auth token.

## Working on documentation

### Using the CI-generated installer for manual testing

After the CI run completes, you can check the output to extract the installer URL:
1. Click into the detailed view of the CI run.
2. Click into any `installer_test` run (the URL you're here to extract will be the same in all of them).
3. Click into the `Run cachix/install-nix-action@v...` step and click the detail triangle next to the first log line (it will also be `Run cachix/install-nix-action@v...`)
4. Copy the value of `install_url`
5. To generate an install command, plug this `install_url` and your GitHub username into this template:

    ```console
    curl -L <install_url> | sh -s -- --tarball-url-prefix https://<github-username>-nix-install-tests.cachix.org/serve
    ```

<!-- #### Manually generating test installers

There's obviously a manual way to do this, and it's still the only way for
platforms that lack GA runners.

I did do this back in Fall 2020 (before the GA approach encouraged here). I'll
sketch what I recall in case it encourages someone to fill in detail, but: I
didn't know what I was doing at the time and had to fumble/ask around a lot--
so I don't want to uphold any of it as "right". It may have been dumb or
the _hard_ way from the getgo. Fundamentals may have changed since.

Here's the build command I used to do this on and for x86_64-darwin:
nix build --out-link /tmp/foo ".#checks.x86_64-darwin.binaryTarball"

I used the stable out-link to make it easier to script the next steps:
link=$(readlink /tmp/foo)
cp $link/*-darwin.tar.xz ~/somewheres

I've lost the last steps and am just going from memory:

From here, I think I had to extract and modify the `install` script to point
it at this tarball (which I scped to my own site, but it might make more sense
to just share them locally). I extracted this script once and then just
search/replaced in it for each new build.

The installer now supports a `--tarball-url-prefix` flag which _may_ have
solved this need?
-->

