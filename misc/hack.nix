# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.96.0-unstable-2026-10-24";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "7229002d4991d0c8c3301cae156d357492c7e246";
    hash = "sha256-OrvfBi44Z6hOOuw4EpbOi7iLsClgIYgvYFYlcM6EBSw=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-lEb+u/Vhqgch1eOP0Ls755m3bNunccZcpUX59ypxOMM=";
  };
})
