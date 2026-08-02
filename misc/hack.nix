# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-01";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "da4a2da865de8b394d2a61f54720990aeef66f1a";
    hash = "sha256-X39fGaM6bR5ZcAKua8Aqrw9721dUoiRjHFlNDfV/oUE=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-fZ1eDFxgB1TGY9k3/c3AJAE5838s4cyqSpj+qPHtRWg=";
  };
})
