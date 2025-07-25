with import ./config.nix;

rec {
  dep1 = mkDerivation {
    name = "check-reqs-dep1";
    builder = builtins.toFile "builder.sh" "mkdir $out; touch $out/file1";
  };

  dep2 = mkDerivation {
    name = "check-reqs-dep2";
    builder = builtins.toFile "builder.sh" "mkdir $out; touch $out/file2";
  };

  deps = mkDerivation {
    name = "check-reqs-deps";
    dep1 = dep1;
    dep2 = dep2;
    builder = builtins.toFile "builder.sh" ''
      mkdir $out
      ln -s $dep1/file1 $out/file1
      ln -s $dep2/file2 $out/file2
    '';
  };

  makeTest = nr: allowreqs: mkDerivation {
    name = "check-reqs-" + toString nr;
    inherit deps;
    builder = builtins.toFile "builder.sh" ''
      mkdir $out
      ln -s $deps $out/depdir1
    '';
    allowedRequisites = allowreqs;
  };

  # When specifying all the requisites, the build succeeds.
  test1 = makeTest 1 [ dep1 dep2 deps ];

  # But missing anything it fails.
  test2 = makeTest 2 [ dep2 deps ];
  test3 = makeTest 3 [ dep1 deps ];
  test4 = makeTest 4 [ deps ];
  test5 = makeTest 5 [];

  test6 = mkDerivation {
    name = "check-reqs";
    inherit deps;
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $deps $out/depdir1";
    disallowedRequisites = [ dep1 dep2 ];
  };

  test7 = mkDerivation {
    name = "check-reqs";
    inherit deps;
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $deps $out/depdir1";
    disallowedRequisites = [test1];
  };

  test8 = mkDerivation {
    name = "check-reqs-structured-attrs";
    __structuredAttrs = true;
    outputChecks.out = {
      allowedRequisites = [dep2];
    };
    inherit dep2;
    outputs = [ "out" ];
    buildCommand = ''
      set -x
      out=''${outputs[out]}
      mkdir $out
      ln -s $dep2 $out/depdir1
      ln -sf $out $out/self-reference
    '';
  };

  test9 = mkDerivation {
    name = "check-reqs-structured-attrs";
    allowedRequisites = [dep2];
    inherit dep2;
    buildCommand = ''
      mkdir $out
      ln -s $dep2 $out/depdir1
      ln -sf $out $out/self-reference
    '';
  };
}
