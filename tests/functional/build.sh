source common.sh

clearStore

# Make sure that 'nix build' returns all outputs by default.
nix build -f multiple-outputs.nix --json a b --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.out | match(".*multiple-outputs-b"))))
'

# Test output selection using the '^' syntax.
nix build -f multiple-outputs.nix --json a^first --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first"]))
'

nix build -f multiple-outputs.nix --json a^second,first --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first", "second"]))
'

nix build -f multiple-outputs.nix --json 'a^*' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first", "second"]))
'

# Test that 'outputsToInstall' is respected by default.
nix build -f multiple-outputs.nix --json e --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b"]))
'

# But not when it's overriden.
nix build -f multiple-outputs.nix --json e^a_a --no-link
nix build -f multiple-outputs.nix --json e^a_a --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a"]))
'

nix build -f multiple-outputs.nix --json 'e^*' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b", "c"]))
'

# test buidling from non-drv attr path

nix build -f multiple-outputs.nix --json 'e.a_a.outPath' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a"]))
'

# Illegal type of string context
expectStderr 1 nix build -f multiple-outputs.nix 'e.a_a.drvPath' \
  | grepQuiet "has a context which refers to a complete source and binary closure."

# No string context
expectStderr 1 nix build --expr '""' --no-link \
  | grepQuiet "has 0 entries in its context. It should only have exactly one entry"

# Too much string context
expectStderr 1 nix build --impure --expr 'with (import ./multiple-outputs.nix).e.a_a; "${drvPath}${outPath}"' --no-link \
  | grepQuiet "has 2 entries in its context. It should only have exactly one entry"

nix build --impure --json --expr 'builtins.unsafeDiscardOutputDependency (import ./multiple-outputs.nix).e.a_a.drvPath' --no-link | jq --exit-status '
  (.[0] | match(".*multiple-outputs-e.drv"))
'

# Test building from raw store path to drv not expression.

drv=$(nix eval -f multiple-outputs.nix --raw a.drvPath)
if nix build "$drv^not-an-output" --no-link --json; then
    fail "'not-an-output' should fail to build"
fi

if nix build "$drv^" --no-link --json; then
    fail "'empty outputs list' should fail to build"
fi

if nix build "$drv^*nope" --no-link --json; then
    fail "'* must be entire string' should fail to build"
fi

nix build "$drv^first" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.first | match(".*multiple-outputs-a-first")) and
      (has("second") | not)))
'

nix build "$drv^first,second" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
'

nix build "$drv^*" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
'

# Make sure that `--impure` works (regression test for https://github.com/NixOS/nix/issues/6488)
nix build --impure -f multiple-outputs.nix --json e --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b"]))
'

# Make sure that `--stdin` works and does not apply any defaults
printf "" | nix build --no-link --stdin --json | jq --exit-status '. == []'
printf "%s\n" "$drv^*" | nix build --no-link --stdin --json | jq --exit-status '.[0]|has("drvPath")'

# URL reporting
out="$(nix-build fod-failing.nix -A x1 2>&1)" && status=0 || status=$?
test "$status" = 102
test "$(<<<"$out" grep -E '^error:' | wc -l)" = 1
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x1\\.drv'"
<<<"$out" grepQuiet -E "likely URL: https://meow.puppy.forge/puppy.tar.gz"

# --keep-going and FOD
out="$(nix build -f fod-failing.nix -L 2>&1)" && status=0 || status=$?
test "$status" = 1
# at least one "hash mismatch" error, one "build of ... failed"
test "$(<<<"$out" grep -E '^error:' | wc -l)" -ge 2
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x.\\.drv'"
<<<"$out" grepQuiet -E "likely URL: "
<<<"$out" grepQuiet -E "error: build of '.*-x[1-4]\\.drv\\^out', '.*-x[1-4]\\.drv\\^out', '.*-x[1-4]\\.drv\\^out', '.*-x[1-4]\\.drv\\^out' failed"

out="$(nix build -f fod-failing.nix -L x1 x2 x3 --keep-going 2>&1)" && status=0 || status=$?
test "$status" = 1
# three "hash mismatch" errors - for each failing fod, one "build of ... failed"
test "$(<<<"$out" grep -E '^error:' | wc -l)" = 4
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x1\\.drv'"
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x3\\.drv'"
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x2\\.drv'"
<<<"$out" grepQuiet -E "likely URL: https://meow.puppy.forge/puppy.tar.gz"
<<<"$out" grepQuiet -E "likely URL: https://kitty.forge/cat.tar.gz"
<<<"$out" grepQuiet -E "likely URL: \(unknown\)"
<<<"$out" grepQuiet -E "error: build of '.*-x[1-3]\\.drv\\^out', '.*-x[1-3]\\.drv\\^out', '.*-x[1-3]\\.drv\\^out' failed"

out="$(nix build -f fod-failing.nix -L x4 2>&1)" && status=0 || status=$?
test "$status" = 1
test "$(<<<"$out" grep -E '^error:' | wc -l)" -ge 2
<<<"$out" grepQuiet -E "error: [12] dependencies of derivation '.*-x4\\.drv' failed to build"
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x[23]\\.drv'"

out="$(nix build -f fod-failing.nix -L x4 --keep-going 2>&1)" && status=0 || status=$?
test "$status" = 1
test "$(<<<"$out" grep -E '^error:' | wc -l)" = 3
<<<"$out" grepQuiet -E "error: 2 dependencies of derivation '.*-x4\\.drv' failed to build"
<<<"$out" grepQuiet -vE "hash mismatch in fixed-output derivation '.*-x3\\.drv'"
<<<"$out" grepQuiet -vE "hash mismatch in fixed-output derivation '.*-x2\\.drv'"

# Ensure when if the system build dir is inaccessible, we can still build things
BUILD_DIR=$(mktemp -d)
chmod 0000 "$BUILD_DIR"
nix --build-dir "$BUILD_DIR" build -E 'with import ./config.nix; mkDerivation { name = "test"; buildCommand = "echo rawr > $out"; }' --impure --no-link

# ensure that the build directory parent is not world-accessible
chmod 0755 "$BUILD_DIR"
FIFO="$BUILD_DIR/fifo"
mkfifo "$FIFO"
(
  echo > "$FIFO"
  trap 'echo > "$FIFO"' EXIT
  mode=$(stat -c %a $BUILD_DIR/b/*)
  [ "$mode" = "700" -o "$mode" = "710" ]
) &
nix build --build-dir "$BUILD_DIR/b" -E '
  with import ./config.nix; mkDerivation {
    name = "test";
    buildCommand = "cat '"$FIFO"'; cat '"$FIFO"' > $out";
  }' \
  --extra-sandbox-paths "$FIFO" \
  --impure \
  --no-link
wait
