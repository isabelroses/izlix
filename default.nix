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

  version = "2.97.0-pre-${lib.concatStrings (lib.takeEnd 3 (lib.splitVersion sourceInfo.version))}-${
    builtins.substring 0 12 sourceInfo.src.rev
  }";

  scope = pkgs.lixPackageSets.makeLixScope {
    attrName = "izlix";

    lix-args = {
      inherit version;
      inherit (sourceInfo) src cargoDeps;

      # nixpkgs defaults this to false, which passes -Dmimalloc=disabled
      useMimalloc = true;

      # newer cooler builds
      useLicxxbridge = true;

      patches = [
        # adds a --call-package or -C cli option to build a package from the cli
        # based on the work of https://github.com/privatevoid-net/nix-super
        ./patches/callpackage-cli.patch

        # don't alter the names of derivations for nix store diff-closure
        ./patches/closure-names.patch

        # adds a nom like logger right into lix
        ./patches/nom-logger.patch

        # add more builtins to lix, this consists of the following:
        # - `builtins.abs` which will get you a absolute value of a number
        ./patches/builtins-abs.patch
        # - `builtins.greaterThan` which will return true if the first argument is greater than the second
        ./patches/builtins-greaterThan.patch
        # - `builtins.pow` which will raise the first argument to the power of the second
        ./patches/builtins-pow.patch
        # - `builtins.mod` will return the remainder of the first argument divided by the second
        ./patches/builtins-mod.patch

        # backport some of nix's newer eval stats stuff. not sure exactly when they
        # were added (cba to find the diff). so lets add them back. i doubt lix would
        # accept this since it touches json stuff
        ./patches/libexpr-backport-new-stats.patch
        # this one isn't *needed* per say but its nice to have for *accuracy* i suppose
        ./patches/libexpr-backport-counter-for-stats.patch

        # don't allocate values in a lamda when _: is the argument
        ./patches/lixexpr-don-t-allocate-on-_.patch

        # build failure on master
        (pkgs.fetchpatch2 {
          url = "https://raw.githubusercontent.com/NixOS/nixpkgs/refs/heads/master/pkgs/tools/package-management/lix/lix-rs-tests-whole-archive.patch";
          hash = "sha256-azPE9n6V29omgDcsGk6krf+P6dXrM9prpCFImhw2n3Y=";
        })
      ];
    };
  };

  finalScope = scope.overrideScope (
    final: prev: {
      capnproto = pkgs.capnproto.overrideAttrs (oa: {
        patches = oa.patches or [ ] ++ [
          # backport of https://github.com/capnproto/capnproto/pull/1810
          ./patches/capnproto-promise-nodiscard.patch
        ];
      });

      lix = (prev.lix.override { withAWS = false; }).overrideAttrs (oa: {
        # Kinda funny right
        # worth it https://akko.isabelroses.com/notice/AjlM7Vfq1zlgsEzk0G
        postPatch = oa.postPatch or "" + ''
          substituteInPlace lix/libmain/shared.cc \
            --replace-fail "(Lix, like Nix)" "(Lix, like Nix but for lesbians)"
        '';

        # seems we still need this
        buildInputs = lib.subtractLists [ final.editline ] oa.buildInputs;

        # these are flakey
        doInstallCheck = false;
      });

      nix-serve-ng = prev.nix-serve-ng.overrideAttrs (_: {
        meta.broken = true;
      });

      nixpkgs-review = prev.nixpkgs-review.override {
        nix-eval-jobs = final.nix-eval-jobs;
      };
    }
  );
in
{
  inherit sourceInfo;
}
// finalScope
