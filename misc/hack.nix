# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-29";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "1d4eda40fe90a7b5fdd76b8cf84752015c61abd2";
    hash = "sha256-zhP7SlMmCrYwMAm411Inpsp0yhtQ2KqtBd6s3rxNZtE=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-43pXxWPzLd6pvD2odx22ckNORPCfrPbOhUTqT3vKEKI=";
  };
})
