# bundle lix source info into one file, we do this to hackily update lix to be
# at the latest nightly because lix does weird things with the meta.position
{
  stdenv,
  fetchFromGitHub,
  rustPlatform,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "lix-source-info";
  version = "2.95.3-unstable-2026-07-13";

  # ideally we want to fetch from gitea, but they seem to have their atom file disabled
  src = fetchFromGitHub {
    owner = "lix-project";
    repo = "lix";
    rev = "782d96734230a1ccef675700cdbc59db4af000f3";
    hash = "sha256-MRTpt/j3Yp/7sbSwKc1lC4mqKoVKy2QCpsnRLIQakgg=";
  };

  cargoDeps = rustPlatform.fetchCargoVendor {
    name = "lix-${finalAttrs.version}";
    inherit (finalAttrs) src;
    hash = "sha256-UnuVmYkD58zN1xl00Fz7DEFIYH64m4ClxCy8V82qnco=";
  };
})
