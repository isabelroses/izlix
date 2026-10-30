# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.96.0-unstable-2026-10-29";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "3c4698152e8d6b72cff7b7f52ae68f69a80a4dab";
    hash = "sha256-h9cN4IDbeHIECDlcO+t+0RFYhMEwMGM3Cnp50JFKMpo=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-lEb+u/Vhqgch1eOP0Ls755m3bNunccZcpUX59ypxOMM=";
  };
})
