# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-12";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "c60a53d1d560a6d03a408cd30c95db1d752e0896";
    hash = "sha256-uipuaI3owf8YEIOtzNrqp+i4dDPtBNjHs/c61gvVJLM=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-hZJGpMnnsk++/OfkAxJ+fVWOK3WqhDiWVqANtdeKcOM=";
  };
})
