# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-30";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "b3f1a4f2a7abdccd4fd991fd6bbf3fcd83798ee5";
    hash = "sha256-zUKmMs1t8XLFH3ZArjzY8ueEShFM3UpoqZdSukaMvfk=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-43pXxWPzLd6pvD2odx22ckNORPCfrPbOhUTqT3vKEKI=";
  };
})
