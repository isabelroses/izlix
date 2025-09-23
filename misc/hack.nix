# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.93.3-unstable-2025-09-22";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "f68233ec4320c75c3e913313f47a06621b1e0dfe";
    hash = "sha256-lE6mqb8aq+1Cp0q8TPyYV1T1JwLxySFVkDO35jgurr4=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-APm8m6SVEAO17BBCka13u85/87Bj+LePP7Y3zHA3Mpg=";
  };
})
