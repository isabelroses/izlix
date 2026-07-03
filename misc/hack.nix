# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-01";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "2c45e056e9b2681d4c340486f1431942d2c0d84c";
    hash = "sha256-IqdxJibSyWym5uufYp31/d0RpAhmX4Q2Czs65aV7sp0=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-8Z4bV7K4f3lAdKu0h4DYgWHKJ9DmRArHZAKhHpUbkuY=";
  };
})
