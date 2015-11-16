import os
import zipfile
import tempfile
import tarfile


def main():
    tmp_dir = tempfile.mkdtemp(prefix='nix-installer-')
    unpack_dir = os.path.join(tmp_dir, 'unpack')

    os.mkdir(unpack_dir)

    ZIP = zipfile.ZipFile(os.path.dirname(__file__))
    ZIP.extractall(tmp_dir)

    TAR = tarfile.open(os.path.join(tmp_dir, 'archive.tar.bz2'), mode='r:bz2')
    TAR.extractall(path=unpack_dir)

    os.system(unpack_dir + "/*/install")


if __name__ == "__main__":
    main()
