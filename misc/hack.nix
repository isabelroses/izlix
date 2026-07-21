# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-20";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "37ff864b79671cead7704d5238383c090760fd7b";
    hash = "sha256-9+9/RAk8x9YEA5sJhMaTKiYTCwhECx5i3tl1iQNHP3s=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-WbSHmK8d8SLF1WqB9NZTBa18/pQSXtnZzygIIc8AEEM=";
  };
})
