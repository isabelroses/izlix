> [!WARNING] This is a not lix! This is my abirtary fork for silly little features I want that are not good for upstream lix.

I add the following patches:

- [callpackage cli](https://github.com/isabelroses/izlix/blob/main/patches/lix-callpackage-cli.patch) - a patch to add `-C` to `nix build` to allow you to build a package from a file.
- [closure names](https://github.com/isabelroses/izlix/blob/main/patches/closure-names.patch) - a patch to not overwrite the closure name when comparing the diff of two closures.
- [abs builtin](https://github.com/isabelroses/izlix/blob/main/patches/lix-feat-builtins-abs.patch) - adds the `abs` builtin
- [greaterThan builtin](https://github.com/isabelroses/izlix/blob/main/patches/lix-feat-builtins-greaterThan.patch) - adds the `greaterThan` builtin
- [pow builtin](https://github.com/isabelroses/izlix/blob/main/patches/lix-feat-builtins-pow.patch) - adds the `pow` builtin

None of the patches should be used in production code but are nice to have.
