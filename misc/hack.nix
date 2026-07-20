# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-19";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "e780cc6c9ac9889aafa226f9dbd4f8cbe35860ac";
    hash = "sha256-Mnjl2vgu8jgVfMRQC+G0jU4sizw1wHDf9d1Lpjx+7ns=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-WbSHmK8d8SLF1WqB9NZTBa18/pQSXtnZzygIIc8AEEM=";
  };
})
