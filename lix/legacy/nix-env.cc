#include "lix/libcmd/cmd-profiles.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/names.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "user-env.hh"
#include "lix/libutil/users.hh"
#include "lix/libexpr/value-to-json.hh"
#include "lix/libutil/xml-writer.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libexpr/eval-settings.hh" // for defexpr
#include "nix-env.hh"

#include <ctime>
#include <algorithm>
#include <iostream>
#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using std::cout;

namespace nix {


typedef enum {
    srcNixExprDrvs,
    srcNixExprs,
    srcStorePaths,
    srcProfile,
    srcAttrPath,
    srcUnknown
} InstallSourceType;


struct InstallSourceInfo
{
    InstallSourceType type;
    std::shared_ptr<SourcePath> nixExprPath; /* for srcNixExprDrvs, srcNixExprs */
    Path profile; /* for srcProfile */
    std::string systemFilter; /* for srcNixExprDrvs */
    Bindings * autoArgs;
};


struct Globals
{
    AsyncIoRoot & aio;
    InstallSourceInfo instSource;
    Path profile;
    std::shared_ptr<Evaluator> state;
    bool dryRun;
    bool preserveInstalled;
    bool removeAll;
    std::string forceName;
    bool prebuiltOnly;
};


typedef void (* Operation) (Globals & globals,
    Strings opFlags, Strings opArgs);


static std::string needArg(Strings::iterator & i,
    Strings & args, const std::string & arg)
{
    if (i == args.end()) throw UsageError("'%1%' requires an argument", arg);
    return *i++;
}


static bool parseInstallSourceOptions(Globals & globals,
    Strings::iterator & i, Strings & args, const std::string & arg)
{
    if (arg == "--from-expression" || arg == "-E")
        globals.instSource.type = srcNixExprs;
    else if (arg == "--from-profile") {
        globals.instSource.type = srcProfile;
        globals.instSource.profile = needArg(i, args, arg);
    }
    else if (arg == "--attr" || arg == "-A")
        globals.instSource.type = srcAttrPath;
    else return false;
    return true;
}


static bool isNixExpr(EvalPaths & paths, const CheckedSourcePath & path, struct InputAccessor::Stat & st)
{
    if (st.type == InputAccessor::tRegular) {
        return true;
    } else if (st.type != InputAccessor::tDirectory) {
        return false;
    } else {
        auto defaultNix = paths.checkSourcePath(path + "default.nix");
        return defaultNix.pathExists();
    }
}


static constexpr size_t maxAttrs = 1024;


static void getAllExprs(Evaluator & state,
    const CheckedSourcePath & path, StringSet & seen, BindingsBuilder & attrs)
{
    StringSet namesSorted;
    for (auto & [name, _] : path.readDirectory()) namesSorted.insert(name);

    for (auto & i : namesSorted) {
        /* Ignore the manifest.nix used by profiles.  This is
           necessary to prevent it from showing up in channels (which
           are implemented using profiles). */
        if (i == "manifest.nix") continue;

        auto path2 = state.paths.checkSourcePath(path + i);

        InputAccessor::Stat st;
        try {
            st = path2.stat();
        } catch (Error &) {
            continue; // ignore dangling symlinks in ~/.nix-defexpr
        }

        if (isNixExpr(state.paths, path2, st) && (st.type != InputAccessor::tRegular || path2.baseName().ends_with(".nix"))) {
            /* Strip off the `.nix' filename suffix (if applicable),
               otherwise the attribute cannot be selected with the
               `-A' option.  Useful if you want to stick a Nix
               expression directly in ~/.nix-defexpr. */
            std::string attrName = i;
            if (attrName.ends_with(".nix"))
                attrName = std::string(attrName, 0, attrName.size() - 4);
            if (!seen.insert(attrName).second) {
                std::string suggestionMessage = "";
                if (path2.to_string().find("channels") != std::string::npos && path.to_string().find("channels") != std::string::npos)
                    suggestionMessage = fmt("\nsuggestion: remove '%s' from either the root channels or the user channels", attrName);
                printError("warning: name collision in input Nix expressions, skipping '%1%'"
                            "%2%", path2, suggestionMessage);
                continue;
            }
            /* Load the expression on demand. */
            auto vArg = state.mem.allocValue();
            vArg->mkString(path2.canonical().abs());
            if (seen.size() == maxAttrs)
                throw Error("too many Nix expressions in directory '%1%'", path);
            attrs.alloc(attrName).mkApp(&state.builtins.get("import"), vArg);
        }
        else if (st.type == InputAccessor::tDirectory)
            /* `path2' is a directory (with no default.nix in it);
               recurse into it. */
            getAllExprs(state, path2, seen, attrs);
    }
}



static void loadSourceExpr(EvalState & state, const SourcePath & path_, Value & v)
{
    auto path = state.ctx.paths.checkSourcePath(path_);
    auto st = path.stat();

    if (isNixExpr(state.ctx.paths, path, st))
        state.evalFile(path, v);

    /* The path is a directory.  Put the Nix expressions in the
       directory in a set, with the file name of each expression as
       the attribute name.  Recurse into subdirectories (but keep the
       set flat, not nested, to make it easier for a user to have a
       ~/.nix-defexpr directory that includes some system-wide
       directory). */
    else if (st.type == InputAccessor::tDirectory) {
        auto attrs = state.ctx.buildBindings(maxAttrs);
        attrs.alloc("_combineChannels").mkList(0);
        StringSet seen;
        getAllExprs(state.ctx, path, seen, attrs);
        v.mkAttrs(attrs);
    }

    else throw Error("path '%s' is not a directory or a Nix expression", path);
}


static void loadDerivations(EvalState & state, const SourcePath & nixExprPath,
    std::string systemFilter, Bindings & autoArgs,
    const std::string & pathPrefix, DrvInfos & elems)
{
    Value vRoot;
    loadSourceExpr(state, nixExprPath, vRoot);

    Value & v(*findAlongAttrPath(state, pathPrefix, autoArgs, vRoot).first);

    getDerivations(state, v, pathPrefix, autoArgs, elems, true);

    /* Filter out all derivations not applicable to the current
       system. */
    for (DrvInfos::iterator i = elems.begin(), j; i != elems.end(); i = j) {
        j = i; j++;
        if (systemFilter != "*" && i->querySystem(state) != systemFilter)
            elems.erase(i);
    }
}


static NixInt getPriority(EvalState & state, DrvInfo & drv)
{
    return drv.queryMetaInt(state, "priority", NixInt(0));
}


static std::strong_ordering comparePriorities(EvalState & state, DrvInfo & drv1, DrvInfo & drv2)
{
    return getPriority(state, drv2) <=> getPriority(state, drv1);
}


// FIXME: this function is rather slow since it checks a single path
// at a time.
static bool isPrebuilt(EvalState & state, DrvInfo & elem)
{
    auto path = elem.queryOutPath(state);
    if (state.aio.blockOn(state.ctx.store->isValidPath(path))) return true;
    return state.aio.blockOn(state.ctx.store->querySubstitutablePaths({path})).count(path);
}


static void checkSelectorUse(DrvNames & selectors)
{
    /* Check that all selectors have been used. */
    for (auto & i : selectors)
        if (i.hits == 0 && i.fullName != "*")
            throw Error("selector '%1%' matches no derivations", i.fullName);
}


namespace {

std::set<std::string> searchByPrefix(EvalState & state, DrvInfos & allElems, std::string_view prefix) {
    constexpr std::size_t maxResults = 3;
    std::set<std::string> result;
    for (auto & drvInfo : allElems) {
        const auto drvName = DrvName { drvInfo.queryName(state) };
        if (drvName.name.starts_with(prefix)) {
            result.emplace(drvName.name);

            if (result.size() >= maxResults) {
                break;
            }
        }
    }
    return result;
}

struct Match
{
    DrvInfo drvInfo;
    std::size_t index;

    Match(DrvInfo drvInfo_, std::size_t index_)
    : drvInfo{std::move(drvInfo_)}
    , index{index_}
    {}
};

/* If a selector matches multiple derivations
   with the same name, pick the one matching the current
   system.  If there are still multiple derivations, pick the
   one with the highest priority.  If there are still multiple
   derivations, pick the one with the highest version.
   Finally, if there are still multiple derivations,
   arbitrarily pick the first one. */
std::vector<Match> pickNewestOnly(EvalState & state, std::vector<Match> matches) {
    /* Map from package names to derivations. */
    std::map<std::string, Match> newest;
    StringSet multiple;

    for (auto & match : matches) {
        auto & oneDrv = match.drvInfo;

        const auto drvName = DrvName { oneDrv.queryName(state) };
        std::strong_ordering comparison = std::strong_ordering::greater;

        const auto itOther = newest.find(drvName.name);

        if (itOther != newest.end()) {
            auto & newestDrv = itOther->second.drvInfo;

            comparison =
                oneDrv.querySystem(state) == newestDrv.querySystem(state) ? std::strong_ordering::equal :
                oneDrv.querySystem(state) == settings.thisSystem ? std::strong_ordering::greater :
                newestDrv.querySystem(state) == settings.thisSystem ? std::strong_ordering::less : std::strong_ordering::equal;
            if (comparison == 0)
                comparison = comparePriorities(state, oneDrv, newestDrv);
            if (comparison == 0)
                comparison = compareVersions(drvName.version, DrvName{newestDrv.queryName(state)}.version);
        }

        if (comparison > 0) {
            newest.erase(drvName.name);
            newest.emplace(drvName.name, match);
            multiple.erase(drvName.fullName);
        } else if (comparison == 0) {
            multiple.insert(drvName.fullName);
        }
    }

    matches.clear();
    for (auto & [name, match] : newest) {
        if (multiple.find(name) != multiple.end())
            warn(
                "there are multiple derivations named '%1%'; using the first one",
                name);
        matches.push_back(match);
    }

    return matches;
}

} // end namespace

static DrvInfos filterBySelector(EvalState & state, DrvInfos & allElems,
    const Strings & args, bool newestOnly)
{
    DrvNames selectors = drvNamesFromArgs(args);
    if (selectors.empty())
        selectors.emplace_back("*");

    DrvInfos elems;
    std::set<std::size_t> done;

    for (auto & selector : selectors) {
        std::vector<Match> matches;
        for (auto && [index, drvInfo] : enumerate(allElems)) {
            const auto drvName = DrvName { drvInfo.queryName(state) };
            if (selector.matches(drvName)) {
                ++selector.hits;
                matches.emplace_back(drvInfo, index);
            }
        }

        if (newestOnly) {
            matches = pickNewestOnly(state, std::move(matches));
        }

        /* Insert only those elements in the final list that we
           haven't inserted before. */
        for (auto & match : matches)
            if (done.insert(match.index).second)
                elems.push_back(match.drvInfo);

        if (selector.hits == 0 && selector.fullName != "*") {
            const auto prefixHits = searchByPrefix(state, allElems, selector.name);

            if (prefixHits.empty()) {
                throw Error("selector '%1%' matches no derivations", selector.fullName);
            } else {
                std::string suggestionMessage = ", maybe you meant:";
                for (const auto & drvName : prefixHits) {
                    suggestionMessage += fmt("\n%s", drvName);
                }
                throw Error("selector '%1%' matches no derivations" + suggestionMessage, selector.fullName);
            }
        }
    }

    return elems;
}


static bool isPath(std::string_view s)
{
    return s.find('/') != std::string_view::npos;
}


static void queryInstSources(EvalState & state,
    InstallSourceInfo & instSource, const Strings & args,
    DrvInfos & elems, bool newestOnly)
{
    InstallSourceType type = instSource.type;
    if (type == srcUnknown && args.size() > 0 && isPath(args.front()))
        type = srcStorePaths;

    switch (type) {

        /* Get the available user environment elements from the
           derivations specified in a Nix expression, including only
           those with names matching any of the names in `args'. */
        case srcUnknown:
        case srcNixExprDrvs: {

            /* Load the derivations from the (default or specified)
               Nix expression. */
            DrvInfos allElems;
            loadDerivations(state, *instSource.nixExprPath,
                instSource.systemFilter, *instSource.autoArgs, "", allElems);

            elems = filterBySelector(state, allElems, args, newestOnly);

            break;
        }

        /* Get the available user environment elements from the Nix
           expressions specified on the command line; these should be
           functions that take the default Nix expression file as
           argument, e.g., if the file is `./foo.nix', then the
           argument `x: x.bar' is equivalent to `(x: x.bar)
           (import ./foo.nix)' = `(import ./foo.nix).bar'. */
        case srcNixExprs: {

            Value vArg;
            loadSourceExpr(state, *instSource.nixExprPath, vArg);

            for (auto & i : args) {
                Expr & eFun = state.ctx.parseExprFromString(i, CanonPath::fromCwd());
                Value vFun, vTmp;
                state.eval(eFun, vFun);
                vTmp.mkApp(&vFun, &vArg);
                getDerivations(state, vTmp, "", *instSource.autoArgs, elems, true);
            }

            break;
        }

        /* The available user environment elements are specified as a
           list of store paths (which may or may not be
           derivations). */
        case srcStorePaths: {

            for (auto & i : args) {
                auto path = state.ctx.store->followLinksToStorePath(i);

                std::string name(path.name());

                DrvInfo elem("", nullptr);
                elem.setName(name);

                if (path.isDerivation()) {
                    elem.setDrvPath(path);
                    auto outputs =
                        state.aio.blockOn(state.ctx.store->queryDerivationOutputMap(path));
                    elem.setOutPath(outputs.at("out"));
                    if (name.size() >= drvExtension.size() &&
                        std::string(name, name.size() - drvExtension.size()) == drvExtension)
                        name = name.substr(0, name.size() - drvExtension.size());
                }
                else
                    elem.setOutPath(path);

                elems.push_back(elem);
            }

            break;
        }

        /* Get the available user environment elements from another
           user environment.  These are then filtered as in the
           `srcNixExprDrvs' case. */
        case srcProfile: {
            auto installed = queryInstalled(state, instSource.profile);
            elems = filterBySelector(
                state,
                installed,
                args, newestOnly
            );
            break;
        }

        case srcAttrPath: {
            Value vRoot;
            loadSourceExpr(state, *instSource.nixExprPath, vRoot);
            for (auto & i : args) {
                Value & v(*findAlongAttrPath(state, i, *instSource.autoArgs, vRoot).first);
                getDerivations(state, v, "", *instSource.autoArgs, elems, true);
            }
            break;
        }
    }
}


static void printMissing(EvalState & state, DrvInfos & elems)
{
    std::vector<DerivedPath> targets;
    for (auto & i : elems)
        if (auto drvPath = i.queryDrvPath(state))
            targets.emplace_back(DerivedPath::Built{
                .drvPath = makeConstantStorePath(*drvPath),
                .outputs = OutputsSpec::All { },
            });
        else
            targets.emplace_back(DerivedPath::Opaque{
                .path = i.queryOutPath(state),
            });

    state.aio.blockOn(printMissing(state.ctx.store, targets));
}


static bool keep(EvalState & state, DrvInfo & drv)
{
    return drv.queryMetaBool(state, "keep", false);
}

static void setMetaFlag(EvalState & state, DrvInfo & drv,
    const std::string & name, const std::string & value)
{
    auto v = state.ctx.mem.allocValue();
    v->mkString(value);
    drv.setMeta(state, name, v);
}

static void installDerivations(Globals & globals,
    const Strings & args, const Path & profile, std::optional<int> priority)
{
    debug("installing derivations");

    auto state = globals.state->begin(globals.aio);

    /* Get the set of user environment elements to be installed. */
    DrvInfos newElems, newElemsTmp;
    queryInstSources(*state, globals.instSource, args, newElemsTmp, true);

    /* If --prebuilt-only is given, filter out source-only packages. */
    for (auto & i : newElemsTmp)
        if (!globals.prebuiltOnly || isPrebuilt(*state, i))
            newElems.push_back(i);

    StringSet newNames;
    for (auto & i : newElems) {
        /* `forceName' is a hack to get package names right in some
           one-click installs, namely those where the name used in the
           path is not the one we want (e.g., `java-front' versus
           `java-front-0.9pre15899'). */
        if (globals.forceName != "")
            i.setName(globals.forceName);
        newNames.insert(DrvName(i.queryName(*state)).name);
    }

    if (priority) {
        for (auto & drv : newElems) {
            setMetaFlag(*state, drv, "priority", std::to_string((priority.value())));
        }
    }

    while (true) {
        auto lockToken = optimisticLockProfile(profile);

        DrvInfos allElems(newElems);

        /* Add in the already installed derivations, unless they have
           the same name as a to-be-installed element. */
        if (!globals.removeAll) {
            DrvInfos installedElems = queryInstalled(*state, profile);

            for (auto & i : installedElems) {
                DrvName drvName(i.queryName(*state));
                if (!globals.preserveInstalled &&
                    newNames.find(drvName.name) != newNames.end() &&
                    !keep(*state, i))
                    printInfo("replacing old '%s'", i.queryName(*state));
                else
                    allElems.push_back(i);
            }

            for (auto & i : newElems)
                printInfo("installing '%s'", i.queryName(*state));
        }

        printMissing(*state, newElems);

        if (globals.dryRun) return;

        if (createUserEnv(*state, allElems,
                profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opInstall(Globals & globals, Strings opFlags, Strings opArgs)
{
    std::optional<int> priority;
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        auto arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else if (arg == "--preserve-installed" || arg == "-P")
            globals.preserveInstalled = true;
        else if (arg == "--remove-all" || arg == "-r")
            globals.removeAll = true;
        else if (arg == "--priority") {
            if (i == opFlags.end())
                throw UsageError("'%1%' requires an argument", arg);
            priority = string2Int<int>(*i++);
            if (!priority)
                throw UsageError("'--priority' requires an integer argument");
        }
        else throw UsageError("unknown flag '%1%'", arg);
    }

    installDerivations(globals, opArgs, globals.profile, priority);
}


typedef enum { utLt, utLeq, utEq, utAlways } UpgradeType;


static void upgradeDerivations(Globals & globals,
    const Strings & args, UpgradeType upgradeType)
{
    debug("upgrading derivations");

    auto state = globals.state->begin(globals.aio);

    /* Upgrade works as follows: we take all currently installed
       derivations, and for any derivation matching any selector, look
       for a derivation in the input Nix expression that has the same
       name and a higher version number. */

    while (true) {
        auto lockToken = optimisticLockProfile(globals.profile);

        DrvInfos installedElems = queryInstalled(*state, globals.profile);

        /* Fetch all derivations from the input file. */
        DrvInfos availElems;
        queryInstSources(*state, globals.instSource, args, availElems, false);

        /* Go through all installed derivations. */
        DrvInfos newElems;
        for (auto & i : installedElems) {
            DrvName drvName(i.queryName(*state));

            try {

                if (keep(*state, i)) {
                    newElems.push_back(i);
                    continue;
                }

                /* Find the derivation in the input Nix expression
                   with the same name that satisfies the version
                   constraints specified by upgradeType.  If there are
                   multiple matches, take the one with the highest
                   priority.  If there are still multiple matches,
                   take the one with the highest version.
                   Do not upgrade if it would decrease the priority. */
                DrvInfos::iterator bestElem = availElems.end();
                std::string bestVersion;
                for (auto j = availElems.begin(); j != availElems.end(); ++j) {
                    if (comparePriorities(*state, i, *j) > 0)
                        continue;
                    DrvName newName(j->queryName(*state));
                    if (newName.name == drvName.name) {
                        std::strong_ordering d = compareVersions(drvName.version, newName.version);
                        if ((upgradeType == utLt && d < 0) ||
                            (upgradeType == utLeq && d <= 0) ||
                            (upgradeType == utEq && d == 0) ||
                            upgradeType == utAlways)
                        {
                            std::strong_ordering d2 = std::strong_ordering::less;
                            if (bestElem != availElems.end()) {
                                d2 = comparePriorities(*state, *bestElem, *j);
                                if (d2 == 0) d2 = compareVersions(bestVersion, newName.version);
                            }
                            if (d2 < 0 && (!globals.prebuiltOnly || isPrebuilt(*state, *j))) {
                                bestElem = j;
                                bestVersion = newName.version;
                            }
                        }
                    }
                }

                if (bestElem != availElems.end() &&
                    i.queryOutPath(*state) !=
                    bestElem->queryOutPath(*state))
                {
                    const char * action = compareVersions(drvName.version, bestVersion) <= 0
                        ? "upgrading" : "downgrading";
                    printInfo("%1% '%2%' to '%3%'",
                        action, i.queryName(*state), bestElem->queryName(*state));
                    newElems.push_back(*bestElem);
                } else newElems.push_back(i);

            } catch (Error & e) {
                e.addTrace(nullptr, "while trying to find an upgrade for '%s'", i.queryName(*state));
                throw;
            }
        }

        printMissing(*state, newElems);

        if (globals.dryRun) return;

        if (createUserEnv(*state, newElems,
                globals.profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opUpgrade(Globals & globals, Strings opFlags, Strings opArgs)
{
    UpgradeType upgradeType = utLt;
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        std::string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else if (arg == "--lt") upgradeType = utLt;
        else if (arg == "--leq") upgradeType = utLeq;
        else if (arg == "--eq") upgradeType = utEq;
        else if (arg == "--always") upgradeType = utAlways;
        else throw UsageError("unknown flag '%1%'", arg);
    }

    upgradeDerivations(globals, opArgs, upgradeType);
}


static void opSetFlag(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());
    if (opArgs.size() < 2)
        throw UsageError("not enough arguments to '--set-flag'");

    Strings::iterator arg = opArgs.begin();
    std::string flagName = *arg++;
    std::string flagValue = *arg++;
    DrvNames selectors = drvNamesFromArgs(Strings(arg, opArgs.end()));

    auto state = globals.state->begin(globals.aio);

    while (true) {
        std::string lockToken = optimisticLockProfile(globals.profile);

        DrvInfos installedElems = queryInstalled(*state, globals.profile);

        /* Update all matching derivations. */
        for (auto & i : installedElems) {
            DrvName drvName(i.queryName(*state));
            for (auto & j : selectors)
                if (j.matches(drvName)) {
                    printInfo("setting flag on '%1%'", i.queryName(*state));
                    j.hits++;
                    setMetaFlag(*state, i, flagName, flagValue);
                    break;
                }
        }

        checkSelectorUse(selectors);

        /* Write the new user environment. */
        if (createUserEnv(*state, installedElems,
                globals.profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opSet(Globals & globals, Strings opFlags, Strings opArgs)
{
    auto state = globals.state->begin(globals.aio);

    auto store2 = globals.state->store.try_cast_shared<LocalFSStore>();
    if (!store2) throw Error("--set is not supported for this Nix store");

    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        std::string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else throw UsageError("unknown flag '%1%'", arg);
    }

    DrvInfos elems;
    queryInstSources(*state, globals.instSource, opArgs, elems, true);

    if (elems.size() != 1)
        throw Error("--set requires exactly one derivation");

    DrvInfo & drv(elems.front());

    if (globals.forceName != "")
        drv.setName(globals.forceName);

    auto drvPath = drv.queryDrvPath(*state);
    std::vector<DerivedPath> paths {
        drvPath
        ? (DerivedPath) (DerivedPath::Built {
            .drvPath = makeConstantStorePath(*drvPath),
            .outputs = OutputsSpec::All { },
        })
        : (DerivedPath) (DerivedPath::Opaque {
            .path = drv.queryOutPath(*state),
        }),
    };
    globals.aio.blockOn(printMissing(globals.state->store, paths));
    if (globals.dryRun) return;
    globals.aio.blockOn(
        globals.state->store->buildPaths(paths, globals.state->repair ? bmRepair : bmNormal)
    );

    debug("switching to new user environment");
    Path generation = globals.aio.blockOn(createGeneration(
        *store2,
        globals.profile,
        drv.queryOutPath(*state)));
    switchLink(globals.profile, generation);
}


static void uninstallDerivations(Globals & globals, Strings & selectors,
    Path & profile)
{
    auto state = globals.state->begin(globals.aio);

    while (true) {
        auto lockToken = optimisticLockProfile(profile);

        DrvInfos workingElems = queryInstalled(*state, profile);

        for (auto & selector : selectors) {
            DrvInfos::iterator split = workingElems.begin();
            if (isPath(selector)) {
                StorePath selectorStorePath = globals.state->store->followLinksToStorePath(selector);
                split = std::partition(
                    workingElems.begin(), workingElems.end(),
                    [&selectorStorePath, &state](auto &elem) {
                        return selectorStorePath != elem.queryOutPath(*state);
                    }
                );
            } else {
                DrvName selectorName(selector);
                split = std::partition(
                    workingElems.begin(), workingElems.end(),
                    [&selectorName, &state](auto &elem){
                        DrvName elemName(elem.queryName(*state));
                        return !selectorName.matches(elemName);
                    }
                );
            }
            if (split == workingElems.end())
                warn("selector '%s' matched no installed derivations", selector);
            for (auto removedElem = split; removedElem != workingElems.end(); removedElem++) {
                printInfo("uninstalling '%s'", removedElem->queryName(*state));
            }
            workingElems.erase(split, workingElems.end());
        }

        if (globals.dryRun) return;

        if (createUserEnv(*state, workingElems,
                profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opUninstall(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());
    uninstallDerivations(globals, opArgs, globals.profile);
}


static bool cmpChars(char a, char b)
{
    return toupper(a) < toupper(b);
}


static bool cmpElemByName(EvalState & state, DrvInfo & a, DrvInfo & b)
{
    auto a_name = a.queryName(state);
    auto b_name = b.queryName(state);
    return lexicographical_compare(
        a_name.begin(), a_name.end(),
        b_name.begin(), b_name.end(), cmpChars);
}


typedef std::list<Strings> Table;


void printTable(Table & table)
{
    auto nrColumns = table.size() > 0 ? table.front().size() : 0;

    std::vector<size_t> widths;
    widths.resize(nrColumns);

    for (auto & i : table) {
        assert(i.size() == nrColumns);
        Strings::iterator j;
        size_t column;
        for (j = i.begin(), column = 0; j != i.end(); ++j, ++column)
            if (j->size() > widths[column]) widths[column] = j->size();
    }

    for (auto & i : table) {
        Strings::iterator j;
        size_t column;
        for (j = i.begin(), column = 0; j != i.end(); ++j, ++column) {
            std::string s = *j;
            replace(s.begin(), s.end(), '\n', ' ');
            cout << s;
            if (column < nrColumns - 1)
                cout << std::string(widths[column] - s.size() + 2, ' ');
        }
        cout << std::endl;
    }
}


/* This function compares the version of an element against the
   versions in the given set of elements.  `cvLess' means that only
   lower versions are in the set, `cvEqual' means that at most an
   equal version is in the set, and `cvGreater' means that there is at
   least one element with a higher version in the set.  `cvUnavail'
   means that there are no elements with the same name in the set. */

typedef enum { cvLess, cvEqual, cvGreater, cvUnavail } VersionDiff;

static VersionDiff compareVersionAgainstSet(
    EvalState & state, DrvInfo & elem, DrvInfos & elems, std::string & version)
{
    DrvName name(elem.queryName(state));

    VersionDiff diff = cvUnavail;
    version = "?";

    for (auto & i : elems) {
        DrvName name2(i.queryName(state));
        if (name.name == name2.name) {
            std::strong_ordering d = compareVersions(name.version, name2.version);
            if (d < 0) {
                diff = cvGreater;
                version = name2.version;
            }
            else if (diff != cvGreater && d == 0) {
                diff = cvEqual;
                version = name2.version;
            }
            else if (diff != cvGreater && diff != cvEqual && d > 0) {
                diff = cvLess;
                if (version == "" || compareVersions(version, name2.version) < 0)
                    version = name2.version;
            }
        }
    }

    return diff;
}


static void queryJSON(EvalState & state, Globals & globals, std::vector<DrvInfo> & elems, bool printOutPath, bool printDrvPath, bool printMeta)
{
    JSON topObj = JSON::object();
    for (auto & i : elems) {
        try {
            if (i.hasFailed()) continue;


            auto drvName = DrvName(i.queryName(state));
            JSON &pkgObj = topObj[i.attrPath];
            pkgObj = {
                {"name", drvName.fullName},
                {"pname", drvName.name},
                {"version", drvName.version},
                {"system", i.querySystem(state)},
                {"outputName", i.queryOutputName(state)},
            };

            {
                DrvInfo::Outputs outputs = i.queryOutputs(state, printOutPath);
                JSON &outputObj = pkgObj["outputs"];
                outputObj = JSON::object();
                for (auto & j : outputs) {
                    if (j.second)
                        outputObj[j.first] = globals.state->store->printStorePath(*j.second);
                    else
                        outputObj[j.first] = nullptr;
                }
            }

            if (printDrvPath) {
                auto drvPath = i.queryDrvPath(state);
                if (drvPath) pkgObj["drvPath"] = globals.state->store->printStorePath(*drvPath);
            }

            if (printMeta) {
                JSON &metaObj = pkgObj["meta"];
                metaObj = JSON::object();
                StringSet metaNames = i.queryMetaNames(state);
                for (auto & j : metaNames) {
                    Value * v = i.queryMeta(state, j);
                    if (!v) {
                        printError("derivation '%s' has invalid meta attribute '%s'", i.queryName(state), j);
                        metaObj[j] = nullptr;
                    } else {
                        NixStringContext context;
                        metaObj[j] = printValueAsJSON(state, true, *v, noPos, context);
                    }
                }
            }
        } catch (AssertionError & e) {
            printMsg(lvlTalkative, "skipping derivation named '%1%' which gives an assertion failure", i.queryName(state));
        } catch (Error & e) {
            e.addTrace(nullptr, "while querying the derivation named '%1%'", i.queryName(state));
            throw;
        }
    }
    std::cout << topObj.dump(2);
}


static void opQuery(Globals & globals, Strings opFlags, Strings opArgs)
{
    auto & store { *globals.state->store };
    auto state = globals.state->begin(globals.aio);

    Strings remaining;
    std::string attrPath;

    bool printStatus = false;
    bool printName = true;
    bool printAttrPath = false;
    bool printSystem = false;
    bool printDrvPath = false;
    bool printOutPath = false;
    bool printDescription = false;
    bool printMeta = false;
    bool compareVersions = false;
    bool xmlOutput = false;
    bool jsonOutput = false;

    enum { sInstalled, sAvailable } source = sInstalled;

    settings.readOnlyMode = true; /* makes evaluation a bit faster */

    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        auto arg = *i++;
        if (arg == "--status" || arg == "-s") printStatus = true;
        else if (arg == "--no-name") printName = false;
        else if (arg == "--system") printSystem = true;
        else if (arg == "--description") printDescription = true;
        else if (arg == "--compare-versions" || arg == "-c") compareVersions = true;
        else if (arg == "--drv-path") printDrvPath = true;
        else if (arg == "--out-path") printOutPath = true;
        else if (arg == "--meta") printMeta = true;
        else if (arg == "--installed") source = sInstalled;
        else if (arg == "--available" || arg == "-a") source = sAvailable;
        else if (arg == "--xml") xmlOutput = true;
        else if (arg == "--json") jsonOutput = true;
        else if (arg == "--attr-path" || arg == "-P") printAttrPath = true;
        else if (arg == "--attr" || arg == "-A")
            attrPath = needArg(i, opFlags, arg);
        else
            throw UsageError("unknown flag '%1%'", arg);
    }

    if (printAttrPath && source != sAvailable)
        throw UsageError("--attr-path(-P) only works with --available");

    /* Obtain derivation information from the specified source. */
    DrvInfos availElems, installedElems;

    if (source == sInstalled || compareVersions || printStatus)
        installedElems = queryInstalled(*state, globals.profile);

    if (source == sAvailable || compareVersions)
        loadDerivations(*state, *globals.instSource.nixExprPath,
            globals.instSource.systemFilter, *globals.instSource.autoArgs,
            attrPath, availElems);

    DrvInfos elems_ = filterBySelector(*state,
        source == sInstalled ? installedElems : availElems,
        opArgs, false);

    DrvInfos & otherElems(source == sInstalled ? availElems : installedElems);


    /* Sort them by name. */
    /* !!! */
    std::vector<DrvInfo> elems;
    for (auto & i : elems_) elems.push_back(i);
    sort(elems.begin(), elems.end(), [&] (auto& a, auto& b) { return cmpElemByName(*state, a, b); });

    /* We only need to know the installed paths when we are querying
       the status of the derivation. */
    StorePathSet installed; /* installed paths */

    if (printStatus)
        for (auto & i : installedElems)
            installed.insert(i.queryOutPath(*state));


    /* Query which paths have substitutes. */
    StorePathSet validPaths;
    StorePathSet substitutablePaths;
    if (printStatus || globals.prebuiltOnly) {
        StorePathSet paths;
        for (auto & i : elems)
            try {
                paths.insert(i.queryOutPath(*state));
            } catch (AssertionError & e) {
                printMsg(lvlTalkative, "skipping derivation named '%s' which gives an assertion failure", i.queryName(*state));
                i.setFailed();
            }
        validPaths = globals.aio.blockOn(store.queryValidPaths(paths));
        substitutablePaths = globals.aio.blockOn(store.querySubstitutablePaths(paths));
    }


    /* Print the desired columns, or XML output. */
    if (jsonOutput) {
        queryJSON(*state, globals, elems, printOutPath, printDrvPath, printMeta);
        cout << '\n';
        return;
    }

    RunPager pager;

    Table table;
    std::ostringstream dummy;
    XMLWriter xml(true, *(xmlOutput ? &cout : &dummy));
    XMLOpenElement xmlRoot(xml, "items");

    for (auto & i : elems) {
        try {
            if (i.hasFailed()) continue;

            //Activity act(*logger, lvlDebug, "outputting query result '%1%'", i.attrPath);

            if (globals.prebuiltOnly &&
                !validPaths.count(i.queryOutPath(*state)) &&
                !substitutablePaths.count(i.queryOutPath(*state)))
                continue;

            /* For table output. */
            Strings columns;

            /* For XML output. */
            XMLAttrs attrs;

            if (printStatus) {
                auto outPath = i.queryOutPath(*state);
                bool hasSubs = substitutablePaths.count(outPath);
                bool isInstalled = installed.count(outPath);
                bool isValid = validPaths.count(outPath);
                if (xmlOutput) {
                    attrs["installed"] = isInstalled ? "1" : "0";
                    attrs["valid"] = isValid ? "1" : "0";
                    attrs["substitutable"] = hasSubs ? "1" : "0";
                } else
                    columns.push_back(
                        (std::string) (isInstalled ? "I" : "-")
                        + (isValid ? "P" : "-")
                        + (hasSubs ? "S" : "-"));
            }

            if (xmlOutput)
                attrs["attrPath"] = i.attrPath;
            else if (printAttrPath)
                columns.push_back(i.attrPath);

            if (xmlOutput) {
                auto drvName = DrvName(i.queryName(*state));
                attrs["name"] = drvName.fullName;
                attrs["pname"] = drvName.name;
                attrs["version"] = drvName.version;
            } else if (printName) {
                columns.push_back(i.queryName(*state));
            }

            if (compareVersions) {
                /* Compare this element against the versions of the
                   same named packages in either the set of available
                   elements, or the set of installed elements.  !!!
                   This is O(N * M), should be O(N * lg M). */
                std::string version;
                VersionDiff diff = compareVersionAgainstSet(*state, i, otherElems, version);

                char ch;
                switch (diff) {
                    case cvLess: ch = '>'; break;
                    case cvEqual: ch = '='; break;
                    case cvGreater: ch = '<'; break;
                    case cvUnavail: ch = '-'; break;
                    default: abort();
                }

                if (xmlOutput) {
                    if (diff != cvUnavail) {
                        attrs["versionDiff"] = ch;
                        attrs["maxComparedVersion"] = version;
                    }
                } else {
                    auto column = (std::string) "" + ch + " " + version;
                    if (diff == cvGreater && shouldANSI(StandardOutputStream::Stdout))
                        column = ANSI_RED + column + ANSI_NORMAL;
                    columns.push_back(column);
                }
            }

            if (xmlOutput) {
                if (i.querySystem(*state) != "") attrs["system"] = i.querySystem(*state);
            }
            else if (printSystem)
                columns.push_back(i.querySystem(*state));

            if (printDrvPath) {
                auto drvPath = i.queryDrvPath(*state);
                if (xmlOutput) {
                    if (drvPath) attrs["drvPath"] = store.printStorePath(*drvPath);
                } else
                    columns.push_back(drvPath ? store.printStorePath(*drvPath) : "-");
            }

            if (xmlOutput)
                attrs["outputName"] = i.queryOutputName(*state);

            if (printOutPath && !xmlOutput) {
                DrvInfo::Outputs outputs = i.queryOutputs(*state);
                std::string s;
                for (auto & j : outputs) {
                    if (!s.empty()) s += ';';
                    if (j.first != "out") { s += j.first; s += "="; }
                    s += store.printStorePath(*j.second);
                }
                columns.push_back(s);
            }

            if (printDescription) {
                auto descr = i.queryMetaString(*state, "description");
                if (xmlOutput) {
                    if (descr != "") attrs["description"] = descr;
                } else
                    columns.push_back(descr);
            }

            if (xmlOutput) {
                XMLOpenElement item(xml, "item", attrs);
                DrvInfo::Outputs outputs = i.queryOutputs(*state, printOutPath);
                for (auto & j : outputs) {
                    XMLAttrs attrs2;
                    attrs2["name"] = j.first;
                    if (j.second)
                        attrs2["path"] = store.printStorePath(*j.second);
                    xml.writeEmptyElement("output", attrs2);
                }
                if (printMeta) {
                    StringSet metaNames = i.queryMetaNames(*state);
                    for (auto & j : metaNames) {
                        XMLAttrs attrs2;
                        attrs2["name"] = j;
                        Value * v = i.queryMeta(*state, j);
                        if (!v)
                            printError(
                                "derivation '%s' has invalid meta attribute '%s'",
                                i.queryName(*state), j);
                        else {
                            if (v->type() == nString) {
                                attrs2["type"] = "string";
                                attrs2["value"] = v->string.s;
                                xml.writeEmptyElement("meta", attrs2);
                            } else if (v->type() == nInt) {
                                attrs2["type"] = "int";
                                attrs2["value"] = fmt("%1%", v->integer);
                                xml.writeEmptyElement("meta", attrs2);
                            } else if (v->type() == nFloat) {
                                attrs2["type"] = "float";
                                attrs2["value"] = fmt("%1%", v->fpoint);
                                xml.writeEmptyElement("meta", attrs2);
                            } else if (v->type() == nBool) {
                                attrs2["type"] = "bool";
                                attrs2["value"] = v->boolean ? "true" : "false";
                                xml.writeEmptyElement("meta", attrs2);
                            } else if (v->type() == nList) {
                                attrs2["type"] = "strings";
                                XMLOpenElement m(xml, "meta", attrs2);
                                for (auto elem : v->listItems()) {
                                    if (elem->type() != nString) continue;
                                    XMLAttrs attrs3;
                                    attrs3["value"] = elem->string.s;
                                    xml.writeEmptyElement("string", attrs3);
                                }
                            } else if (v->type() == nAttrs) {
                                attrs2["type"] = "strings";
                                XMLOpenElement m(xml, "meta", attrs2);
                                Bindings & attrs = *v->attrs;
                                for (auto &i : attrs) {
                                    Attr & a(*attrs.find(i.name));
                                    if(a.value->type() != nString) continue;
                                    XMLAttrs attrs3;
                                    attrs3["type"] = globals.state->symbols[i.name];
                                    attrs3["value"] = a.value->string.s;
                                    xml.writeEmptyElement("string", attrs3);
                            }
                            }
                        }
                    }
                }
            } else
                table.push_back(columns);

            cout.flush();

        } catch (AssertionError & e) {
            printMsg(lvlTalkative, "skipping derivation named '%1%' which gives an assertion failure", i.queryName(*state));
        } catch (Error & e) {
            e.addTrace(nullptr, "while querying the derivation named '%1%'", i.queryName(*state));
            throw;
        }
    }

    if (!xmlOutput) printTable(table);
}


static void opSwitchProfile(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError("exactly one argument expected");

    Path profile = absPath(opArgs.front());
    Path profileLink = settings.useXDGBaseDirectories ? createNixStateDir() + "/profile" : getHome() + "/.nix-profile";

    switchLink(profileLink, profile);
}


static void opSwitchGeneration(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError("exactly one argument expected");

    if (auto dstGen = string2Int<GenerationNumber>(opArgs.front()))
        switchGeneration(globals.profile, dstGen, globals.dryRun);
    else
        throw UsageError("expected a generation number");
}


static void opRollback(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError("no arguments expected");

    switchGeneration(globals.profile, {}, globals.dryRun);
}


static void opListGenerations(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError("no arguments expected");

    PathLock lock = lockProfile(globals.profile);

    auto [gens, curGen] = findGenerations(globals.profile);

    RunPager pager;

    for (auto & i : gens) {
        tm t;
        if (!localtime_r(&i.creationTime, &t)) throw Error("cannot convert time");
        logger->cout("%|4|   %|4|-%|02|-%|02| %|02|:%|02|:%|02|   %||",
            i.number,
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec,
            i.number == curGen ? "(current)" : "");
    }
}


static void opDeleteGenerations(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError("unknown flag '%1%'", opFlags.front());

    if (opArgs.size() == 1 && opArgs.front() == "old") {
        deleteOldGenerations(globals.profile, globals.dryRun);
    } else if (opArgs.size() == 1 && opArgs.front().find('d') != std::string::npos) {
        auto t = parseOlderThanTimeSpec(opArgs.front());
        deleteGenerationsOlderThan(globals.profile, t, globals.dryRun);
    } else if (opArgs.size() == 1 && opArgs.front().find('+') != std::string::npos) {
        if (opArgs.front().size() < 2)
            throw Error("invalid number of generations '%1%'", opArgs.front());
        auto str_max = opArgs.front().substr(1);
        auto max = string2Int<GenerationNumber>(str_max);
        if (!max)
            throw Error("invalid number of generations to keep '%1%'", opArgs.front());
        deleteGenerationsGreaterThan(globals.profile, *max, globals.dryRun);
    } else {
        std::set<GenerationNumber> gens;
        for (auto & i : opArgs) {
            if (auto n = string2Int<GenerationNumber>(i))
                gens.insert(*n);
            else
                throw UsageError("invalid generation number '%1%'", i);
        }
        deleteGenerations(globals.profile, gens, globals.dryRun);
    }
}


static void opVersion(Globals & globals, Strings opFlags, Strings opArgs)
{
    printVersion("nix-env");
}


static int main_nix_env(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        Strings opFlags, opArgs;
        Operation op = 0;
        std::string opName;
        bool showHelp = false;
        std::string file;

        Globals globals{aio, {}, {}, {}, {}, {}, {}, {}, {}};

        globals.instSource.type = srcUnknown;
        globals.instSource.systemFilter = "*";

        Path nixExprPath = getNixDefExpr();

        if (!pathExists(nixExprPath)) {
            try {
                createDirs(nixExprPath);
                replaceSymlink(
                    defaultChannelsDir(),
                    nixExprPath + "/channels");
                if (getuid() != 0)
                    replaceSymlink(
                        rootChannelsDir(),
                        nixExprPath + "/channels_root");
            } catch (Error &) { }
        }

        globals.dryRun = false;
        globals.preserveInstalled = false;
        globals.removeAll = false;
        globals.prebuiltOnly = false;

        struct MyArgs : LegacyArgs, MixEvalArgs
        {
            using LegacyArgs::LegacyArgs;
        };

        MyArgs myArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            Operation oldOp = op;

            if (*arg == "--help")
                showHelp = true;
            else if (*arg == "--version")
                op = opVersion;
            else if (*arg == "--install" || *arg == "-i") {
                op = opInstall;
                opName = "-install";
            }
            else if (*arg == "--force-name") // undocumented flag for nix-install-package
                globals.forceName = getArg(*arg, arg, end);
            else if (*arg == "--uninstall" || *arg == "-e") {
                op = opUninstall;
                opName = "-uninstall";
            }
            else if (*arg == "--upgrade" || *arg == "-u") {
                op = opUpgrade;
                opName = "-upgrade";
            }
            else if (*arg == "--set-flag") {
                op = opSetFlag;
                opName = arg->substr(1);
            }
            else if (*arg == "--set") {
                op = opSet;
                opName = arg->substr(1);
            }
            else if (*arg == "--query" || *arg == "-q") {
                op = opQuery;
                opName = "-query";
            }
            else if (*arg == "--profile" || *arg == "-p")
                globals.profile = absPath(getArg(*arg, arg, end));
            else if (*arg == "--file" || *arg == "-f")
                file = getArg(*arg, arg, end);
            else if (*arg == "--switch-profile" || *arg == "-S") {
                op = opSwitchProfile;
                opName = "-switch-profile";
            }
            else if (*arg == "--switch-generation" || *arg == "-G") {
                op = opSwitchGeneration;
                opName = "-switch-generation";
            }
            else if (*arg == "--rollback") {
                op = opRollback;
                opName = arg->substr(1);
            }
            else if (*arg == "--list-generations") {
                op = opListGenerations;
                opName = arg->substr(1);
            }
            else if (*arg == "--delete-generations") {
                op = opDeleteGenerations;
                opName = arg->substr(1);
            }
            else if (*arg == "--dry-run") {
                printInfo("(dry run; not doing anything)");
                globals.dryRun = true;
            }
            else if (*arg == "--system-filter")
                globals.instSource.systemFilter = getArg(*arg, arg, end);
            else if (*arg == "--prebuilt-only" || *arg == "-b")
                globals.prebuiltOnly = true;
            else if (*arg != "" && arg->at(0) == '-') {
                opFlags.push_back(*arg);
                /* FIXME: hacky */
                if (*arg == "--from-profile" ||
                    (op == opQuery && (*arg == "--attr" || *arg == "-A")) ||
                    (op == opInstall && (*arg == "--priority")))
                    opFlags.push_back(getArg(*arg, arg, end));
            }
            else
                opArgs.push_back(*arg);

            if (oldOp && oldOp != op)
                throw UsageError("only one operation may be specified");

            return true;
        });

        myArgs.parseCmdline(argv);

        if (showHelp) showManPage("nix-env" + opName);
        if (!op) throw UsageError("no operation specified");

        auto store = aio.blockOn(openStore());

        globals.state = std::make_shared<Evaluator>(aio, myArgs.searchPath, store);
        globals.state->repair = myArgs.repair;

        globals.instSource.nixExprPath = std::make_shared<SourcePath>(
            file != ""
            ? aio.blockOn(lookupFileArg(*globals.state, file)).unwrap()
            : CanonPath(nixExprPath));

        globals.instSource.autoArgs = myArgs.getAutoArgs(*globals.state);

        if (globals.profile == "")
            globals.profile = getEnv("NIX_PROFILE").value_or("");

        if (globals.profile == "")
            globals.profile = getDefaultProfile();

        op(globals, std::move(opFlags), std::move(opArgs));

        globals.state->maybePrintStats();

        return 0;
    }
}

void registerLegacyNixEnv() {
    LegacyCommandRegistry::add("nix-env", main_nix_env);
}

}
