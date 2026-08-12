# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-11";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "9cbe9d5b468496eeac1bd7184004e0d7ff878a06";
    hash = "sha256-9R0krBqEbKhlqFaQ30pZN4EqtWvaCLktFZ0MIx6LBfY=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-fLps3kY5g7NkQM4KKWxrbuCiAyCU2McyMoJ8waLOyQU=";
  };
})
