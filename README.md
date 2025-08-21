> [!WARNING]
> This is a not lix! This is my abirtary fork for silly little features I want that are not good for upstream lix.

I add the following patches:

- [callpackage cli](https://github.com/isabelroses/izlix/blob/main/patches/callpackage-cli.patch) - a patch to add `-C` to `nix build` to allow you to build a package from a file.
- [closure names](https://github.com/isabelroses/izlix/blob/main/patches/closure-names.patch) - a patch to not overwrite the closure name when comparing the diff of two closures.

- [abs builtin](https://github.com/isabelroses/izlix/blob/main/patches/builtins-abs.patch) - adds the `abs` builtin
- [greaterThan builtin](https://github.com/isabelroses/izlix/blob/main/patches/builtins-greaterThan.patch) - adds the `greaterThan` builtin
- [pow builtin](https://github.com/isabelroses/izlix/blob/main/patches/builtins-pow.patch) - adds the `pow` builtin
- [mod builtin](https://github.com/isabelroses/izlix/blob/main/patches/builtins-mod.patch) - adds the `mod` builtin

- [hyperlink attr names to their definition locations in repl](https://gerrit.lix.systems/c/lix/+/3790)
- [nix flake check: Skip substitutable derivation](https://gerrit.lix.systems/c/lix/+/3841)
- [libexpr: enable parallel marking in boehmgc](https://gerrit.lix.systems/c/lix/+/3880), committed manually due to changes in `package.nix`

Builtins can also be found in [lix-math](https://github.com/isabelroses/lix-math).

None of the patches should be used in production code but are nice to have.
