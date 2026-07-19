# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-18";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "ea31dbe5011f4495a40e8daadd2d0eb5c3607eef";
    hash = "sha256-/+g5XLMa9Fz2pJex4iRbcN1QXsA+DAxbmKM/3Bzou/c=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-WbSHmK8d8SLF1WqB9NZTBa18/pQSXtnZzygIIc8AEEM=";
  };
})
