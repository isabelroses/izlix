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
      version = "2.96.0-pre-${lib.concatStrings (lib.takeEnd 3 (lib.splitVersion sourceInfo.version))}-${
        builtins.substring 0 12 sourceInfo.src.rev
      }";
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

        # build against minalloc. port of
        # <https://github.com/NixOS/nix/pull/15596> for lix
        ./patches/build-use-minalloc.patch

        # backport some of nix's newer eval stats stuff. not sure exactly when they
        # were added (cba to find the diff). so lets add them back. i doubt lix would
        # accept this since it touches json stuff
        ./patches/backport-nix-eval-stats.patch

        # nix flake check: Skip substitute derivations
        # https://gerrit.lix.systems/c/lix/+/3841
        # (pkgs.fetchpatch2 {
        #   url = "https://gerrit.lix.systems/changes/lix~3841/revisions/9/patch?download&raw";
        #   hash = "sha256-LKtEAGYpKzCAbgpsYwDyUF0LFZcCXec+D3nxGs+M2eg=";
        #   excludes = [ "doc/manual/change-authors.yml" ];
        # })

        # the next 3 patches change the repl to a replxx backend
        #
        # https://gerrit.lix.systems/c/lix/+/5534
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~5534/revisions/6/patch?download&raw";
          hash = "sha256-vYVjCiapp0WPTCm/YvdiyJNuRaEEkGHyha2DvRpglQc=";
        })

        # https://gerrit.lix.systems/c/lix/+/5515
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~5515/revisions/2/patch?download&raw";
          hash = "sha256-mm3r8kZBRpmys7h1e37WS/zZdNYosEaxMJfzEFgbXys=";
        })

        # https://gerrit.lix.systems/c/lix/+/5570
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~5570/revisions/1/patch?download&raw";
          hash = "sha256-kIX5WJzdA82wfyBkmy616++2IOEDcq4uQFDsX9i08aM=";
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

        buildInputs = [
          # for build minalloc patch
          pkgs.mimalloc

          # we need to add replxx for the replxx patches. but we also remove
          # editline from the build so we don't have to bother building lix's
          # custom editline
          pkgs.replxx
        ]
        ++ (lib.subtractLists [ final.editline ] oa.buildInputs);

        # these are flakey
        doInstallCheck = false;
      });

      nil = prev.nil.overrideAttrs (
        finalAttrs: _: {
          src = pkgs.fetchFromGitHub {
            owner = "oxalica";
            repo = "nil";
            rev = "504599f7e555a249d6754698473124018b80d121";
            hash = "sha256-18j8X2Nbe0Wg1+7YrWRlYzmjZ5Wq0NCVwJHJlBIw/dc=";
          };

          cargoDeps = pkgs.rustPlatform.fetchCargoVendor {
            inherit (finalAttrs) src;
            hash = "sha256-LS2IW4gZ1k6Xl5weMNwxvVA2z56r4rPkjqrkROZTmBw=";
          };
        }
      );
    }
  );
in
{
  inherit sourceInfo;
}
// finalScope
