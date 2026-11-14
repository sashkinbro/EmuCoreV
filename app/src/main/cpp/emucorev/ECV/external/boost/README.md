# Boost libraries - trimmed down for Vita3K

This is a subset of Boost v1.89.0 generated using the bcp tool. To get a list of boost modules guaranteed to exist, check the build script.
Adapted from [citra-emu/ext-boost](https://github.com/citra-emu/ext-boost).

## Updating this repo

To update the Boost version (or to add a new library) follow these steps:

- Download [`boost`](https://boost.org) and extract the package.
- Copy `update.sh` or `update.cmd` in this repo to extracted directory of `boost`.
- `cd` to extracted `boost` directory and run `update` script. The trimed down version of `boost` will be in the `subsets-boost` directory.
- Copy `update.sh`, `update.bat`, `.gitignore`, and `README.md` in this repo to subsets-boost directory.
- Remove all the files in this directory (except the `.git` directory).
- Copy all files in `subsets-boost` to this repo directory.
- Commit all changes.