# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.94.0-unstable-2026-02-03";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "8b73cbbb67621934ba065c2e4f581fe3988bd7e7";
    hash = "sha256-3MZ9z9axqXek+FsAsUt7e/GNRYm49OYqli4d3UvI0sY=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-a5XtutX+NS4wOqxeqbscWZMs99teKick5+cQfbCRGxQ=";
  };
})
