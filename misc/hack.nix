# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-06-30";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "418918b599b8c87322b7ef25a0ab6c5e6c3da9b6";
    hash = "sha256-1QpN1Hyf1TIkqbC5ioHwRY3yje9hx+sbJdh2hnrKvPg=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-8Z4bV7K4f3lAdKu0h4DYgWHKJ9DmRArHZAKhHpUbkuY=";
  };
})
