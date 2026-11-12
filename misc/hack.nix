# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.96.0-unstable-2026-11-12";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "33c14cfacb649bc875e3a8f122f6c80477bcb697";
    hash = "sha256-YMfOx58xxdaFUZ40InUTs00tVFpcihH6QTq0eoHcBJg=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-lEb+u/Vhqgch1eOP0Ls755m3bNunccZcpUX59ypxOMM=";
  };
})
