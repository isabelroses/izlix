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

        # TODO: upstream this?
        # a port of <https://github.com/NixOS/nix/pull/13800> for lix
        # ./patches/wal-mode-for-sqlite-cache-databases.patch

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
        # (pkgs.fetchpatch2 {
        #   name = "warn-import-from-derivation";
        #   url = "https://gerrit.lix.systems/changes/lix~3879/revisions/10/patch?download&raw";
        #   hash = "sha256-3h00IuMlFZHWLPs6EfScDlN46+dTB5qrhM6l2Dw1PHI=";
        #   excludes = [ "doc/manual/change-authors.yml" ];
        # })

        # nix flake check: Skip substitute derivations
        # https://gerrit.lix.systems/c/lix/+/3841
        # (pkgs.fetchpatch2 {
        #   url = "https://gerrit.lix.systems/changes/lix~3841/revisions/9/patch?download&raw";
        #   hash = "sha256-LKtEAGYpKzCAbgpsYwDyUF0LFZcCXec+D3nxGs+M2eg=";
        #   excludes = [ "doc/manual/change-authors.yml" ];
        # })
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

        # these are flakey
        doInstallCheck = false;

        nativeBuildInputs = oa.nativeBuildInputs or [ ] ++ [
          # fixes https://github.com/isabelroses/izlix/actions/runs/20767364744/job/59636260900
          pkgs.rust-cbindgen
        ];
      });
    }
  );
in
{
  inherit sourceInfo;
}
// finalScope
