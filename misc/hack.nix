# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-21";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "eb7009316b012d48a82735e36bc9485a32558658";
    hash = "sha256-7qgPwwlb0d5OnO4GWS6iOSc6zXwAq7I8IlYgi+uytdo=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-MWCXXwmGOGeXFKkfhzoa2zKGp9ciF+PRr4NVWqmujkA=";
  };
})
