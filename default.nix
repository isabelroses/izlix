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

  version = "2.96.0-pre-${lib.concatStrings (lib.takeEnd 3 (lib.splitVersion sourceInfo.version))}-${
    builtins.substring 0 12 sourceInfo.src.rev
  }";

  scope = pkgs.lixPackageSets.makeLixScope {
    attrName = "izlix";

    lix-args = {
      inherit version;
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
        ./patches/libexpr-backport-new-stats.patch
        # this one isn't *needed* per say but its nice to have for *accuracy* i suppose
        ./patches/libexpr-backport-counter-for-stats.patch

        # don't allocate values in a lamda when _: is the argument
        ./patches/lixexpr-don-t-allocate-on-_.patch

        # libfetchers/cache: retry inserting entries into the fetcher cache
        # https://gerrit.lix.systems/c/lix/+/5081
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~5081/revisions/1/patch?download&raw";
          hash = "sha256-g0iJDU9WxJAOymduMJSwrzAxtS8p12ftjcgwJ+Oh9/M=";
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

      lix = (prev.lix.override { withAWS = false; }).overrideAttrs (
        oa:
        let
          cxxLinkerFor = stdenv: lib.getExe' stdenv.cc "${stdenv.cc.targetPrefix}c++";
          hostCargoEnvVar = pkgs.stdenv.hostPlatform.rust.cargoEnvVarTarget;
          buildCargoEnvVar = pkgs.stdenv.buildPlatform.rust.cargoEnvVarTarget;
        in
        {
          # Kinda funny right
          # worth it https://akko.isabelroses.com/notice/AjlM7Vfq1zlgsEzk0G
          postPatch = oa.postPatch or "" + ''
            substituteInPlace lix/libmain/shared.cc \
              --replace-fail "(Lix, like Nix)" "(Lix, like Nix but for lesbians)"
          '';

          buildInputs = [
            # for build minalloc patch
            pkgs.mimalloc
          ]
          ++ (lib.subtractLists [ final.editline ] oa.buildInputs);

          nativeBuildInputs = [
            pkgs.cargo
            pkgs.rustPlatform.cargoSetupHook
          ]
          ++ (lib.subtractLists [ pkgs.rust-cbindgen ] oa.nativeBuildInputs);

          env =
            oa.env
            // {
              "CARGO_TARGET_${hostCargoEnvVar}_LINKER" = cxxLinkerFor pkgs.clangStdenv;
            }
            // lib.optionalAttrs (hostCargoEnvVar != buildCargoEnvVar) {
              "CARGO_TARGET_${buildCargoEnvVar}_LINKER" = cxxLinkerFor pkgs.buildPackages.clangStdenv;
            };

          depsBuildBuild = [
            pkgs.buildPackages.clangStdenv.cc
          ];

          # these are flakey
          doInstallCheck = false;
        }
      );

      nil = prev.nil.overrideAttrs (
        finalAttrs: prevAttrs: {
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

          patches = prevAttrs.patches or [ ] ++ [
            ./patches/nil-feat-inherit-completion.patch
          ];
        }
      );

      nix-serve-ng = prev.nix-serve-ng.overrideAttrs (prevAttrs: {
        meta.broken = true;
      });

      nix-eval-jobs = prev.nix-eval-jobs.overrideAttrs (prevAttrs: {
        patchFlags = [ "-p3" ];

        patches = prevAttrs.patches or [ ] ++ [
          # add the apply flag to nix-eval-jobs
          # <https://git.lix.systems/lix-project/lix/issues/1214>
          ./patches/nix-eval-jobs-apply.patch
        ];
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
