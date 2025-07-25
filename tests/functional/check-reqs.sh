source common.sh

clearStore

RESULT=$TEST_ROOT/result

nix-build -o $RESULT check-reqs.nix -A test1

(! nix-build -o "$RESULT" check-reqs.nix -A test2)
(! nix-build -o "$RESULT" check-reqs.nix -A test3)
(! nix-build -o "$RESULT" check-reqs.nix -A test4) 2>&1 | grepQuiet 'check-reqs-dep1'
(! nix-build -o "$RESULT" check-reqs.nix -A test4) 2>&1 | grepQuiet 'check-reqs-dep2'
(! nix-build -o "$RESULT" check-reqs.nix -A test5)
(! nix-build -o "$RESULT" check-reqs.nix -A test6)

(! nix-build -o "$RESULT" check-reqs.nix -A test6) 2>&1 | grepQuiet '└───.*/.*-check-reqs-deps'
(! nix-build -o "$RESULT" check-reqs.nix -A test6) 2>&1 | grepQuiet 'check-reqs-dep1'
(! nix-build -o "$RESULT" check-reqs.nix -A test6) 2>&1 | grepQuiet 'check-reqs-dep2'

nix-build -o $RESULT check-reqs.nix -A test7

# ignoreSelfRefs is only true for drvs using structuredAttrs.
(! nix-build -o $RESULT check-reqs.nix -A test8)
nix-build -o $RESULT check-reqs.nix -A test9
