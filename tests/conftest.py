import os
import pytest
import shlex
import subprocess
import tempfile


# TODO
#
# - Syntax check scripts first
# - common setup from common.sh as pytest fixtures?


def pytest_collect_file(parent, path):
    if path.ext == ".sh" and path.basename.startswith("test"):
        return BashFile(path, parent)
    if (path.ext == ".nix" and path.basename.startswith("eval-") or
        path.basename.startswith("parse-")):
        return NixFile(path, parent)


class BashFile(pytest.File):

    def collect(self):
        yield BashItem(self.fspath.basename, self)


class BashItem(pytest.Item):

    def runtest(self):
        self.spec = "asdf"
        os.chdir(self.fspath.dirname)
        # try:
        #     subprocess.Popen(['bash', '-n', str(self.fspath)],
        #                      stderr=
        # except subprocess.CalledProcessError as e:
        #     raise BashSyntaxError(output)
        try:
            env = os.environ.copy()
            env['PS4'] = '${BASH_SOURCE}:${LINENO}: '
            output = subprocess.check_output(
                ['bash', '-ex', str(self.fspath)], env=env)
        except subprocess.CalledProcessError as e:
            raise BashException(e)

    def repr_failure(self, excinfo):
        """ called when self.runtest() raises an exception. """
        e = excinfo.value
        if isinstance(e, BashException):
            return "Bash test script exited with exit code: {}".format(
                e.args[0].returncode)
        if isinstance(e, BashSyntaxError):
            return "Bash script had syntax error:\n{}".format(e.args[0])
        return str(e)


    def reportinfo(self):
        return "", 0, str(self.fspath)


class BashException(Exception):
    """ custom exception for error reporting. """

class BashSyntaxError(Exception):
    """ custom exception for error reporting. """




class NixFile(pytest.File):

    def collect(self):
        yield NixItem(self.fspath.basename, self)



class NixItem(pytest.Item):

    def setup(self):
        os.chdir(self.fspath.dirname)
        f, self.stdout = tempfile.mkstemp()
        os.close(f)
        f, self.stderr = tempfile.mkstemp()
        os.close(f)

    def teardown(self):
        os.unlink(self.stdout)
        os.unlink(self.stderr)

    def runtest(self):
        mode, expectation = str(self.fspath.basename).split('-')[:2]
        assert expectation in ['okay', 'fail']

        additional_flags = []
        if mode == 'eval':
            additional_flags.append('--strict')

        flagsfile = os.path.splitext(str(self.fspath.basename))[0] + '.flags'
        flagsfile = self.fspath.dirname + '/' + flagsfile
        if os.path.exists(flagsfile):
            test_flags = open(flagsfile, 'r').read()
            test_flags = shlex.split(test_flags)
            additional_flags.extend(test_flags)

        environment = os.environ.copy()
        environfile = os.path.splitext(str(self.fspath.basename))[0] + '.env'
        environfile = self.fspath.dirname + '/' + environfile
        if os.path.exists(environfile):
            code = open(environfile, 'r').read()
            exec code in {}, environment

        p = subprocess.Popen(['nix-instantiate'] +
                              additional_flags +
                              ['--{}'.format(mode),
                               str(self.fspath)],
                             env=environment,
                             stdout=open(self.stdout, 'w'),
                             stderr=open(self.stderr, 'w'))
        p.communicate()
        returncode = p.wait()

        if expectation == 'fail':
            assert returncode != 0
        else:
            expected_stdout = str(self.fspath).rsplit('.nix')[0] + '.exp'
            if os.path.exists(expected_stdout):
                assert (open(self.stdout, 'r').read() ==
                        open(expected_stdout, 'r').read())
            assert returncode == 0

    def repr_failure(self, excinfo):
        """ called when self.runtest() raises an exception. """
        e = excinfo.value
        return pytest.Item.repr_failure(self, excinfo)

    def reportinfo(self):
        return "", 0, str(self.fspath)
