# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-23";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "ccb43e9284d689cd3e08d7f9b9533b1227666b83";
    hash = "sha256-nTziXUUBHuhFpy7SUJG/pguNBmE3pB82RkSXcJr+3RE=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-MWCXXwmGOGeXFKkfhzoa2zKGp9ciF+PRr4NVWqmujkA=";
  };
})
