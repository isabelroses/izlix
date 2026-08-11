# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-10";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "ee252770f6623414a8553c3e47723eb3a7ef876c";
    hash = "sha256-W6kNN/266x+2Myob6nZsZW1wGQoi7m0xcziIhu6Dk3c=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-fLps3kY5g7NkQM4KKWxrbuCiAyCU2McyMoJ8waLOyQU=";
  };
})
