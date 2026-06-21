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
        # (pkgs.fetchpatch2 {
        #   url = "https://gerrit.lix.systems/changes/lix~5534/revisions/9/patch?download&raw";
        #   hash = "sha256-10da554RvZoAcIvyyf8mHRX78Y4uwHoUY7DP5eQAHGI=";
        # })
        #
        # # https://gerrit.lix.systems/c/lix/+/5515
        # (pkgs.fetchpatch2 {
        #   url = "https://gerrit.lix.systems/changes/lix~5515/revisions/3/patch?download&raw";
        #   hash = "sha256-4lI8TqVogFZ1nZZpLgp3hKY2fCd1oyYT+dFMK4N0Gy8=";
        # })
        #
        # # https://gerrit.lix.systems/c/lix/+/5570
        # (pkgs.fetchpatch2 {
        #   url = "https://gerrit.lix.systems/changes/lix~5570/revisions/2/patch?download&raw";
        #   hash = "sha256-jsjjhJ6jLBDpg9g6htgLAT20Pi/r521B29Gi4T+fuKY=";
        # })

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

          #   # we need to add replxx for the replxx patches. but we also remove
          #   # editline from the build so we don't have to bother building lix's
          #   # custom editline
          #   (pkgs.replxx.overrideAttrs (prev: {
          #     src = pkgs.fetchFromForgejo {
          #       domain = "git.lix.systems";
          #       owner = "lix-project";
          #       repo = "replxx";
          #       rev = "1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2";
          #       hash = "sha256-rXjNicE0Ed5Lwyyv61QAWhDuxIv4208u7//MK67uexc";
          #     };
          #   }))
          # ]
          # ++ (lib.subtractLists [ final.editline ] oa.buildInputs);
        ]
        ++ oa.buildInputs;

        # these are flakey
        doInstallCheck = false;
      });

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
