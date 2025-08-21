{
  pkgs ? import <nixpkgs> {
    inherit system;
    overlays = [ ];
    config.allowUnfree = true;
  },
  lib ? pkgs.lib,
  system ? builtins.currentSystem,
}:
let
  sourceInfo = pkgs.callPackage ./misc/hack.nix { };

  scope = pkgs.lixPackageSets.makeLixScope {
    attrName = "izlix";

    lix-args = {
      version = "2.94.0-pre-${builtins.substring 0 7 sourceInfo.src.rev}";
      inherit (sourceInfo) src cargoDeps;

      patches = [
        # adds a --call-package or -C cli option to build a package from the cli
        # based on the work of https://github.com/privatevoid-net/nix-super
        ./patches/callpackage-cli.patch

        # don't alter the names of derivations for nix store diff-closure
        ./patches/closure-names.patch

        # add more builtins to lix, this consists of the following:
        # - `builtins.abs` which will get you a absolute value of a number
        ./patches/builtins-abs.patch
        # - `builtins.greaterThan` which will return true if the first argument is greater than the second
        ./patches/builtins-greaterThan.patch
        # - `builtins.pow` which will raise the first argument to the power of the second
        ./patches/builtins-pow.patch
        # - `builtins.mod` will return the remainder of the first argument divided by the second
        ./patches/builtins-mod.patch

        # the bellow two patches conflict with each other
        #
        #  warn when encountering IFD with
        # https://gerrit.lix.systems/c/lix/+/3879
        # (fetchpatch {
        #   url = "https://gerrit.lix.systems/changes/lix~3879/revisions/5/patch?download";
        #   decode = "base64 --decode";
        #   hash = "sha256-8hMMb/fMqLv1rxIkE3Ib+KGcgqK2CCDc0hI8LCkvJuw=";
        # })

        # nix flake check: Skip substitute derivations
        # https://gerrit.lix.systems/c/lix/+/3841
        (pkgs.fetchpatch {
          url = "https://gerrit.lix.systems/changes/lix~3841/revisions/7/patch?download";
          decode = "base64 --decode";
          hash = "sha256-HX2co8RAEUdSKsKNb+Fzbhgd3t3hexMJiuoZl9Li6Gw=";
        })

        # improve eval speed significantly by enabling parallel gc
        # https://gerrit.lix.systems/c/lix/+/3880
        (pkgs.fetchpatch {
          url = "https://gerrit.lix.systems/changes/lix~3880/revisions/1/patch?download";
          decode = "base64 --decode";
          hash = "sha256-357bk6yXbRwkkHeukh14QTk0rcNOzmz/zjJTwHdIXI4=";
        })
      ];
    };
  };

  finalScope = scope.overrideScope (
    final: prev: {
      toml11 = final.callPackage ./misc/toml11.nix { };

      boehmgc = (pkgs.boehmgc.override { enableLargeConfig = true; }).overrideAttrs {
        # Increase the initial mark stack size to avoid stack
        # overflows, since these inhibit parallel marking (see
        # GC_mark_some()). To check whether the mark stack is too
        # small, run Nix with GC_PRINT_STATS=1 and look for messages
        # such as `Mark stack overflow`, `No room to copy back mark
        # stack`, and `Grew mark stack to ... frames`.
        NIX_CFLAGS_COMPILE = "-DINITIAL_MARK_STACK_SIZE=1048576";
      };

      lix = prev.lix.overrideAttrs (oa: {
        # Kinda funny right
        # worth it https://akko.isabelroses.com/notice/AjlM7Vfq1zlgsEzk0G
        postPatch = oa.postPatch or "" + ''
          substituteInPlace lix/libmain/shared.cc \
            --replace-fail "(Lix, like Nix)" "(Lix, like Nix but for lesbians)"
        '';

        # these are flakey
        doInstallCheck = false;
      });
    }
  );
in
{
  inherit sourceInfo;
}
// finalScope
