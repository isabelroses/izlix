# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-24";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "a78d68300b5436f57ac43aa49fb49195d14e199d";
    hash = "sha256-6Ynmy55XUPKHU7uMXduJSJEiGe5UkW+VTsJ3yYQKrZ8=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-MWCXXwmGOGeXFKkfhzoa2zKGp9ciF+PRr4NVWqmujkA=";
  };
})
