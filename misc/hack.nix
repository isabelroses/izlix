# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-18";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "3470ce4d506416a1d70f6d3efec454e768aaae58";
    hash = "sha256-Xv0pguwNvT+gbnR5pMsR9RULuAmP0GMtTNd1eHkLoBQ=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-hZJGpMnnsk++/OfkAxJ+fVWOK3WqhDiWVqANtdeKcOM=";
  };
})
