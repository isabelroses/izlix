# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-08-09";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "a8c074e6f7cd7cb61d399d593ca7f739b74895c9";
    hash = "sha256-KS8Fr59OQUR18qY6PFyM5j+Z8AfKxkt69UiG8vk2d70=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-fLps3kY5g7NkQM4KKWxrbuCiAyCU2McyMoJ8waLOyQU=";
  };
})
