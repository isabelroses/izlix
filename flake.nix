{
  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz";
  };

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;

      forAllSystems =
        fn: lib.genAttrs lib.systems.flakeExposed (system: fn nixpkgs.legacyPackages.${system});
    in
    {
      legacyPackages = forAllSystems (pkgs: import ./default.nix { inherit pkgs; });

      packages = forAllSystems (
        pkgs:
        lib.filterAttrs (
          _: pkg:
          let
            isDerivation = lib.isDerivation pkg;
            availableOnHost = lib.meta.availableOn pkgs.stdenv.hostPlatform pkg;
            isBroken = pkg.meta.broken or false;
          in
          isDerivation && !isBroken && availableOnHost
        ) self.legacyPackages.${pkgs.stdenv.hostPlatform.system}
      );

      hydraJobs = forAllSystems (
        pkgs:
        lib.filterAttrs (
          _: pkg:
          let
            isDerivation = lib.isDerivation pkg;
            availableOnHost = lib.meta.availableOn pkgs.stdenv.hostPlatform pkg;
            isCross = pkg.stdenv.buildPlatform != pkg.stdenv.targetPlatform;
            isBroken = pkg.meta.broken or false;
            isCacheable = !(pkg.preferLocalBuild or false);
            isSourceOnly = pkg.pname == "lix-source-info";
          in
          isDerivation && (availableOnHost || isCross) && !isBroken && isCacheable && !isSourceOnly
        ) self.legacyPackages.${pkgs.stdenv.hostPlatform.system}
      );
    };
}
