import os
import pytest
import subprocess



# Syntax check scripts first
#


def pytest_collect_file(parent, path):
    if path.ext == ".sh" and path.basename.startswith("test"):
        return BashFile(path, parent)


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
            output = subprocess.check_output(['bash', '-ex', str(self.fspath)])
        except subprocess.CalledProcessError as e:
            raise BashException(e)
        # for name, value in self.spec.items():
        #     # some custom test execution (dumb example follows)
        #     if name != value:
        #         raise YamlException(self, name, value)

    def repr_failure(self, excinfo):
        """ called when self.runtest() raises an exception. """
        e = excinfo.value
        if isinstance(e, BashException):
            return "Bash test script exited with exit code: {}".format(
                e.args[0].returncode)
        if isinstance(e, BashSyntaxError):
            return "Bash script had syntax error:\n{}".format(e.args[0])


    def reportinfo(self):
        return "", 0, str(self.fspath)


class BashException(Exception):
    """ custom exception for error reporting. """

class BashSyntaxError(Exception):
    """ custom exception for error reporting. """
