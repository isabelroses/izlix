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
    rev = "7d47b392c315d2c85ac24d076b72e33862a01616";
    hash = "sha256-QeDlDehjprjm07ij35ouR3FkqbG6Pim7dQcJ3OIALKc=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-fLps3kY5g7NkQM4KKWxrbuCiAyCU2McyMoJ8waLOyQU=";
  };
})
