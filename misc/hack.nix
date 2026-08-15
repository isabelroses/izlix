# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-14";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "2c5e34618338fac200b85f85ed1d4895c57f06cf";
    hash = "sha256-vQgcCCEM4ppbVMYhdNcx0hA4+sk/RxF1ek5sfQ5mNhE=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-hZJGpMnnsk++/OfkAxJ+fVWOK3WqhDiWVqANtdeKcOM=";
  };
})
