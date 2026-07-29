# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-28";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "64c99ac9af9c83b66643f46e9c8e50ab9f5e6e58";
    hash = "sha256-lJvNaGpFFIooWl/+CLCQBa6sgLbemLF+TS9OqhWOch4=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-43pXxWPzLd6pvD2odx22ckNORPCfrPbOhUTqT3vKEKI=";
  };
})
