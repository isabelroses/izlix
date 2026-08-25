# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-22";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "562630e26e4e459193a4769a16239d16084bca4a";
    hash = "sha256-PqACplKGHZIWDgwv40PIZpU3eXX9fQiJ2lkhU1Kh7UA=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-aLLl8Zp9txhAxlBj55lOo25NSz0z33pg1XDdEdCppy8=";
  };
})
