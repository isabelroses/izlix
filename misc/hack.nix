# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.94.0-unstable-2026-02-02";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "3d77ee8d94b3e8370bd85cd1430dd14dd475c3a7";
    hash = "sha256-N7iv6Q+Xi1RMeBDY6c7RzuqNywxzvCa6kAZHfoOcyE0=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-a5XtutX+NS4wOqxeqbscWZMs99teKick5+cQfbCRGxQ=";
  };
})
