# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.93.3-unstable-2025-11-05";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "24054c110787d725ac0bbdafd40c71c83bf1f9a0";
    hash = "sha256-vFSL9+KvrckyLJB4AEgbAEyKgkaBYBVN6ZDUAa+4Yps=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-APm8m6SVEAO17BBCka13u85/87Bj+LePP7Y3zHA3Mpg=";
  };
})
