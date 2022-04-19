# Overview

Nix consists of layers that operate fairly independently.

At the top is the *command line interface*.

Below that is the Nix *expression language*, in which packages and configurations are written.

The command line and expression language are what users interact with most.

Below that is the *store layer*, Nix' machinery to represent tracked files, dependencies, and fully elaborated build plans.
It is also used for executing those build plans.
The store layer may not be as visible, but this is the heart of Nix.

This chapter describes Nix starting with that bottom store layer, then working its way up until it reaches the more user-facing interfaces described in the rest of the manual."
