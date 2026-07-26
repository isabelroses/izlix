# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-25";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "326fd84d3e7da0d0b00f5f2b72c4300812e26576";
    hash = "sha256-7imce21SglnodNgi7eiZAmZucz5cJFaFpffCWOXT8Xc=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-MWCXXwmGOGeXFKkfhzoa2zKGp9ciF+PRr4NVWqmujkA=";
  };
})
