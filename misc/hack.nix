# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-14";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "f51148a75bd991fcf9545bb11a0626879795537c";
    hash = "sha256-Ax+ETbOHatrUnnQqLEwFMAlb+MwJrIjN88c5gTSDRc8=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-UnuVmYkD58zN1xl00Fz7DEFIYH64m4ClxCy8V82qnco=";
  };
})
