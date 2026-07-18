# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-17";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "1e48ac50ca50abdc36d8702c643019c5947047c9";
    hash = "sha256-9pkKi1IjgRIO6CaH35Sb4VEpzyXrlAVJraUgdW3KjW8=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-s6AP94AgiDkJEtSdEcanEjadpgBnKiQM4UeCw0P28Uk=";
  };
})
