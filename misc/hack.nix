# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-09-02";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "7059b2106bc0a9dfcdd74636b4ee60bfda5f6561";
    hash = "sha256-eKXcJ+lk86wb/FN+mtPSFgfaXsdeI43AS5Z42B/SlME=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-aLLl8Zp9txhAxlBj55lOo25NSz0z33pg1XDdEdCppy8=";
  };
})
