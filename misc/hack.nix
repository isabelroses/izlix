# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-26";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "220cfc000d4c9dbff3949bd4ab6d769f686d78c2";
    hash = "sha256-DRiFKqM+jrrVJa88Ieu86vMNnZzxRS1BrO5eEGiqNgw=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-43pXxWPzLd6pvD2odx22ckNORPCfrPbOhUTqT3vKEKI=";
  };
})
