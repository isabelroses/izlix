# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-22";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "b931753e2e4e93528cb45638b979db5c081d0786";
    hash = "sha256-GdEy4ovot2Wm87TgTKz5Qn9shuCxJr70gqJB9EkDwyg=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-MWCXXwmGOGeXFKkfhzoa2zKGp9ciF+PRr4NVWqmujkA=";
  };
})
