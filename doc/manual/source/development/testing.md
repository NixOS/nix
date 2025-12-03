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
> │   ├── meson.build
> │   ├── include/nix/expr/value/context.hh
> │   ├── value/context.cc
> │   …
> │
> ├── tests
> │   │
> │   …
> │   ├── libutil-tests
> │   │   ├── meson.build
> │   │   …
> │   │   └── data
> │   │       ├── git/tree.txt
> │   │       …
> │   │
> │   ├── libexpr-test-support
> │   │   ├── meson.build
> │   │   ├── include/nix/expr
> │   │   │   ├── meson.build
> │   │   │   └── tests
> │   │   │       ├── value/context.hh
> │   │   │       …
> │   │   └── tests
> │   │       ├── value/context.cc
> │   │       …
> │   │
> │   ├── libexpr-tests
> │   …   ├── meson.build
> │       ├── value/context.cc
> │       …
> …
> ```

The tests for each Nix library (`libnixexpr`, `libnixstore`, etc..) live inside a directory `src/${library_name_without-nix}-test`.
Given an interface (header) and implementation pair in the original library, say, `src/libexpr/include/nix/expr/value/context.hh` and `src/libexpr/value/context.cc`, we write tests for it in `src/libexpr-tests/value/context.cc`, and (possibly) declare/define additional interfaces for testing purposes in `src/libexpr-test-support/include/nix/expr/tests/value/context.hh` and `src/libexpr-test-support/tests/value/context.cc`.

Data for unit tests is stored in a `data` subdir of the directory for each unit test executable.
For example, `libnixstore` code is in `src/libstore`, and its test data is in `src/libstore-tests/data`.
The path to the `src/${library_name_without-nix}-test/data` directory is passed to the unit test executable with the environment variable `_NIX_TEST_UNIT_DATA`.
Note that each executable only gets the data for its tests.

The unit test libraries are in `src/${library_name_without-nix}-test-support`.
All headers are in a `tests` subdirectory so they are included with `#include "nix/tests/"`.

The use of all these separate directories for the unit tests might seem inconvenient, as for example the tests are not "right next to" the part of the code they are testing.
But organizing the tests this way has one big benefit:
there is no risk of any build-system wildcards for the library accidentally picking up test code that should not built and installed as part of the library.

### Running tests

You can run the whole testsuite with `meson test` from the Meson build directory, or the tests for a specific component with `meson test nix-store-tests`.
A environment variables that Google Test accepts are also worth knowing:

1. [`GTEST_FILTER`](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests)

   This is used for finer-grained filtering of which tests to run.


2. [`GTEST_BRIEF`](https://google.github.io/googletest/advanced.html#suppressing-test-passes)

   This is used to avoid logging passing tests.

3. [`GTEST_BREAK_ON_FAILURE`](https://google.github.io/googletest/advanced.html#turning-assertion-failures-into-break-points)

   This is used to create a debugger breakpoint when an assertion failure occurs.

Putting the first two together, one might run

```bash
GTEST_BRIEF=1 GTEST_FILTER='ErrorTraceTest.*' meson test nix-expr-tests -v
```

for short but comprensive output.

### Debugging tests

For debugging, it is useful to combine the third option above with Meson's [`--gdb`](https://mesonbuild.com/Unit-tests.html#other-test-options) flag:

```bash
GTEST_BRIEF=1 GTEST_FILTER='Group.my-failing-test' meson test nix-expr-tests --gdb
```

This will:

1. Run the unit test with GDB

2. Run just `Group.my-failing-test`

3. Stop the program when the test fails, allowing the user to then issue arbitrary commands to GDB.

### Characterisation testing { #characterisation-testing-unit }

See [functional characterisation testing](#characterisation-testing-functional) for a broader discussion of characterisation testing.

Like with the functional characterisation, `_NIX_TEST_ACCEPT=1` is also used.
For example:
```shell-session
$ _NIX_TEST_ACCEPT=1 meson test nix-store-tests -v
...
[  SKIPPED ] WorkerProtoTest.string_read
[  SKIPPED ] WorkerProtoTest.string_write
[  SKIPPED ] WorkerProtoTest.storePath_read
[  SKIPPED ] WorkerProtoTest.storePath_write
...
```
will regenerate the "golden master" expected result for the `libnixstore` characterisation tests.
The characterisation tests will mark themselves "skipped" since they regenerated the expected result instead of actually testing anything.

### JSON Schema testing

In `doc/manual/source/protocols/json/` we have a number of manual pages generated from [JSON Schema](https://json-schema.org/).
That JSON schema is tested against the JSON file test data used in [characterisation tests](#characterisation-testing-unit ) for JSON (de)serialization, in `src/json-schema-checks`.
Between the JSON (de)serialization testing, and this testing of the same data against the schema, we make sure that the manual, the implementation, and a machine-readable schema are are all in sync.

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

The functional tests reside under the `tests/functional` directory and are listed in `tests/functional/meson.build`.
Each test is a bash script.

Functional tests are run during `installCheck` in the `nix` package build, as well as separately from the build, in VM tests.

### Running the whole test suite

The whole test suite (functional and unit tests) can be run with:

```shell-session
$ checkPhase
```

### Grouping tests

Sometimes it is useful to group related tests so they can be easily run together without running the entire test suite.
Each test group is in a subdirectory of `tests`.
For example, `tests/functional/ca/meson.build` defines a `ca` test group for content-addressing derivation outputs.

That test group can be run like this:

```shell-session
$ meson test --suite ca
ninja: Entering directory `/home/jcericson/src/nix/master/build'
ninja: no work to do.
[1-20/20] 🌑 nix-functional-tests:ca / ca/why-depends                                1/20 nix-functional-tests:ca / ca/nix-run                                  OK               0.16s
[2-20/20] 🌒 nix-functional-tests:ca / ca/why-depends                                2/20 nix-functional-tests:ca / ca/import-derivation                        OK               0.17s
```

### Running individual tests

Individual tests can be run with `meson`:

```shell-session
$ meson test --verbose ${testName}
ninja: Entering directory `/home/jcericson/src/nix/master/build'
ninja: no work to do.
1/1 nix-functional-tests:main / ${testName}        OK               0.41s

Ok:                 1
Expected Fail:      0
Fail:               0
Unexpected Pass:    0
Skipped:            0
Timeout:            0

Full log written to /home/jcericson/src/nix/master/build/meson-logs/testlog.txt
```

The `--verbose` flag will make Meson also show the console output of each test for easier debugging.
The test script will then be traced with `set -x` and the output displayed as it happens,
regardless of whether the test succeeds or fails.

Tests can be also run directly without `meson`:

```shell-session
$ TEST_NAME=${testName} NIX_REMOTE='' PS4='+(${BASH_SOURCE[0]-$0}:$LINENO) tests/functional/${testName}.sh
+(${testName}.sh:1) foo
output from foo
+(${testName}.sh:2) bar
output from bar
...
```

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

Then, running the test with [`--interactive`](https://mesonbuild.com/Unit-tests.html#other-test-options) will prevent Meson from hijacking the terminal so you can drop you into GDB once the script reaches that point:

```shell-session
$ meson test ${testName} --interactive
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
_NIX_TEST_ACCEPT=1 meson test lang
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

### Running functional tests on NixOS

We run the functional tests not just in the build, but also in VM tests.
This helps us ensure that Nix works correctly on NixOS, and environments that have similar characteristics that are hard to reproduce in a build environment.

These can be run with:

```shell
nix build .#hydraJobs.tests.functional_user
```

Generally, this build is sufficient, but in nightly or CI we also test the attributes `functional_root` and `functional_trusted`, in which the test suite is run with different levels of authorization.

## Integration tests

The integration tests are defined in the Nix flake under the `hydraJobs.tests` attribute.
These tests include everything that needs to interact with external services or run Nix in a non-trivial distributed setup.
Because these tests are expensive and require more than what the standard github-actions setup provides, they only run on the master branch (on <https://hydra.nixos.org/jobset/nix/master>).

You can run them manually with `nix build .#hydraJobs.tests.{testName}` or `nix-build -A hydraJobs.tests.{testName}`.

## Installer tests

After a one-time setup, the Nix repository's GitHub Actions continuous integration (CI) workflow can test the installer each time you push to a branch.

Creating a Cachix cache for your installer tests and adding its authorisation token to GitHub enables [two installer-specific jobs in the CI workflow](https://github.com/NixOS/nix/blob/88a45d6149c0e304f6eb2efcc2d7a4d0d569f8af/.github/workflows/ci.yml#L50-L91):

- The `installer` job generates installers for the platforms below and uploads them to your Cachix cache:
  - `x86_64-linux`
  - `armv6l-linux`
  - `armv7l-linux`
  - `x86_64-darwin`

- The `installer_test` job (which runs on `ubuntu-24.04` and `macos-14`) will try to install Nix with the cached installer and run a trivial Nix command.

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

