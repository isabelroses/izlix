# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-08";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "41bad096e38dd45e3a0c6832ff26ff7df3fb7eef";
    hash = "sha256-E367vKoJtwwJmFmd3LIOS/pD9HARqrpdqlsWWGgb6mM=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-kXMPsczi6XDgQdc/RTRpcLx4vyD0/vg2TnuNxNF1qlU=";
  };
})
