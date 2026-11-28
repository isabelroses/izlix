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

        # eval speedups
        # - only work out which formals went unused when reporting an error
        ./patches/libexpr-unused-formals-on-error-path.patch
        # - don't zero io buffers we are about to read into
        ./patches/libutil-dont-zero-io-buffers.patch

        # build speedup, keeps the zngur bridge header out of most translation units
        ./patches/libutil-error-hh-drop-lix-rs-include.patch

        # primops: use rust toml parser instead of toml11
        # <https://gerrit.lix.systems/c/lix/+/6712>
        # (pkgs.fetchpatch2 {
        #   url = "https://gerrit.lix.systems/changes/lix~6712/revisions/6/patch?download&raw";
        #   hash = "sha256-tf+2wWA8T2b1KCSv3KrZuiKe0pZdMym9P08w5DC81Vo=";
        # })

        # libcmd/repl: better quoted attrs completion
        # <https://gerrit.lix.systems/c/lix/+/5515>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~5515/revisions/3/patch?download&raw";
          hash = "sha256-4lI8TqVogFZ1nZZpLgp3hKY2fCd1oyYT+dFMK4N0Gy8=";
        })

        # gc surfaces paths it failed to delete (with a macOS TCC hint) and counts
        # deleted paths accurately. this is a stack, so order matters
        # <https://gerrit.lix.systems/c/lix/+/6546>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~6546/revisions/1/patch?download&raw";
          hash = "sha256-xh3Ig/3/RrYQoE/feFSjpFuvTAJP3pyruw5dI6iJ9Jw=";
        })
        # <https://gerrit.lix.systems/c/lix/+/6547>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~6547/revisions/1/patch?download&raw";
          hash = "sha256-sibERP1I3vARjSyIWeoF69AjEIMYL0aKqztr4c5TvZ8=";
        })
        # <https://gerrit.lix.systems/c/lix/+/6548>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~6548/revisions/1/patch?download&raw";
          hash = "sha256-tIQKeofxxBVIesPUotcUjGbyc22SDRI+7u+bHaSEhTY=";
        })

        # legacy/nix-shell: do not run shellHook at shebang time
        # <https://gerrit.lix.systems/c/lix/+/6208>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~6208/revisions/1/patch?download&raw";
          hash = "sha256-u+uPITCgPhJb7K0YST/j1YheiN8Y0fngbP1dXyc4AA4=";
        })

        # don't error on --arg/--argstr when using -f/--expr instead of a flake
        # <https://gerrit.lix.systems/c/lix/+/2609>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~2609/revisions/1/patch?download&raw";
          hash = "sha256-DHGfyDsoPTjjtqJQu5BEpKzd5GaSEwx9eKFxSubjMnQ=";
        })

        # libstore: propagate auto-GC failures to waiters
        # <https://gerrit.lix.systems/c/lix/+/6812>
        (pkgs.fetchpatch2 {
          url = "https://gerrit.lix.systems/changes/lix~6812/revisions/1/patch?download&raw";
          hash = "sha256-/eqUZLFbDtdXsG/Q24UIe9pyRoS4DxXW0hAQxyq2Qco=";
        })

        # libstore: read stderr while waiting for authentication, so the ssh banner
        # shows up before you touch your yubikey and not after
        # rebased from <https://gerrit.lix.systems/c/lix/+/6609/9>: dropped the
        # legacy-ssh-store.cc hunks (that store is gone) and fixed flushLine
        # touching recentStderr after it's reset
        ./patches/libstore-read-ssh-stderr-during-auth.patch

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
