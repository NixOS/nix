from setuptools import setup, find_packages

setup(
    name="hello-nix",
    version="0.1",
    packages=find_packages(),
    entry_points={
        'console_scripts': [
            'hello-nix = hello:greet',
        ],
    },
    #install_requires=['nix'],
)
