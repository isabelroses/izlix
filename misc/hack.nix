# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.96.0-unstable-2026-11-19";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "4da55a9b9352af33a283bcc8ea88bdec4a7b3a49";
    hash = "sha256-Az9lbzkw5ybk55ARndaOhX4EkVZmkjhU61W0CRKfFg0=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-lEb+u/Vhqgch1eOP0Ls755m3bNunccZcpUX59ypxOMM=";
  };
})
