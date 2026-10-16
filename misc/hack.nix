# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.96.0-unstable-2026-10-14";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "3c5f629014e92037f9e446863441bcc5ba94eae3";
    hash = "sha256-q1afbP0uTTIFPUC4KMHbta+61saOMKQTsRbdvOs7rTw=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-aLLl8Zp9txhAxlBj55lOo25NSz0z33pg1XDdEdCppy8=";
  };
})
