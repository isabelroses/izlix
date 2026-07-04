# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-04";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "c1370bc81c102a8d4ed605b299a9508d121e7c5b";
    hash = "sha256-A+gVks/h656opZUA/IJYKoljWwLUMNWVg70NmeozJtU=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-8Z4bV7K4f3lAdKu0h4DYgWHKJ9DmRArHZAKhHpUbkuY=";
  };
})
