# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.96.0-unstable-2026-11-23";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "7092827ac04ebf0763aa9cf8375b9d77514758ae";
    hash = "sha256-3002NmxTM5pvzrm6IQmXzz5Z1z1ZYWdhE9Kp1vxcoTA=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-lEb+u/Vhqgch1eOP0Ls755m3bNunccZcpUX59ypxOMM=";
  };
})
