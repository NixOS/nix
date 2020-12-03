# Overview

Nix is broken into layers that operate fairly independently.

At the top is the *command line interface*, i.e. the argument parsing of the various Nix executables.

Below that is the Nix *expression language*, in which packages and configurations are written.
These are the layers which users interact with most.

Below that is the *store layer*, Nix's machinery for presenting and files and fully elaborated build plans, and also executing those build plans.
The store layer may not be as visible, but this is the heart of Nix.

This chapter will start there and work up towards the more user-facing interfaces described in the rest of the manual.
