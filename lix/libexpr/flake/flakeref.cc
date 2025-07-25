#include "lix/libexpr/flake/flakeref.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/url.hh"
#include "lix/libutil/url-parts.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/registry.hh"

namespace nix {

#if 0
// 'dir' path elements cannot start with a '.'. We also reject
// potentially dangerous characters like ';'.
const static std::string subDirElemRegex = "(?:[a-zA-Z0-9_-]+[a-zA-Z0-9._-]*)";
const static std::string subDirRegex = subDirElemRegex + "(?:/" + subDirElemRegex + ")*";
#endif

std::string FlakeRef::to_string() const
{
    std::map<std::string, std::string> extraQuery;
    if (subdir != "")
        extraQuery.insert_or_assign("dir", subdir);
    return input.toURLString(extraQuery);
}

fetchers::Attrs FlakeRef::toAttrs() const
{
    auto attrs = input.toAttrs();
    if (subdir != "")
        attrs.emplace("dir", subdir);
    return attrs;
}

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef)
{
    str << flakeRef.to_string();
    return str;
}

bool FlakeRef::operator ==(const FlakeRef & other) const
{
    return input == other.input && subdir == other.subdir;
}

kj::Promise<Result<FlakeRef>> FlakeRef::resolve(ref<Store> store) const
try {
    auto [input2, extraAttrs] = TRY_AWAIT(lookupInRegistries(store, input));
    co_return FlakeRef(
        std::move(input2), fetchers::maybeGetStrAttr(extraAttrs, "dir").value_or(subdir)
    );
} catch (...) {
    co_return result::current_exception();
}

FlakeRef parseFlakeRef(
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(url, baseDir, allowMissing, isFlake);
    if (fragment != "")
        throw Error("unexpected fragment '%s' in flake reference '%s'", fragment, url);
    return flakeRef;
}

std::optional<FlakeRef> maybeParseFlakeRef(
    const std::string & url, const std::optional<Path> & baseDir)
{
    try {
        return parseFlakeRef(url, baseDir);
    } catch (Error &) {
        return {};
    }
}

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    using namespace fetchers;

    static std::string fnRegex = "[0-9a-zA-Z-._~!$&'\"()*+,;=]+";

    static std::regex pathUrlRegex = regex::parse(
        "(/?" + fnRegex + "(?:/" + fnRegex + ")*/?)"
        + "(?:\\?(" + queryRegex + "))?"
        + "(?:#(" + queryRegex + "))?",
        std::regex::ECMAScript);

    static std::regex flakeShorthandRegex = regex::parse(
        flakeShorthandRegexS
        + "(?:#(" + queryRegex + "))?",
        std::regex::ECMAScript);

    std::smatch match;

    /* Check if 'url' is a flake ID. This is an abbreviated syntax for
       'flake:<flake-id>?ref=<ref>&rev=<rev>'. */

    if (std::regex_match(url, match, flakeShorthandRegex)) {
        auto parsedURL = ParsedURL{
            .url = url,
            .base = "flake:" + match.str(1),
            .scheme = "flake",
            .authority = "",
            .path = match[1],
        };

        return std::make_pair(
            FlakeRef(Input::fromURL(parsedURL, isFlake), ""),
            percentDecode(match.str(6)));
    }

    else if (std::regex_match(url, match, pathUrlRegex)) {
        std::string path = match[1];
        std::string fragment = percentDecode(match.str(3));

        if (baseDir) {
            /* Check if 'url' is a path (either absolute or relative
               to 'baseDir'). If so, search upward to the root of the
               repo (i.e. the directory containing .git). */

            path = absPath(path, baseDir);

            if (isFlake) {

                if (!allowMissing && !pathExists(path + "/flake.nix")){
                    notice("path '%s' does not contain a 'flake.nix', searching up",path);

                    // Save device to detect filesystem boundary
                    dev_t device = lstat(path).st_dev;
                    bool found = false;
                    while (path != "/") {
                        if (pathExists(path + "/flake.nix")) {
                            found = true;
                            break;
                        } else if (pathExists(path + "/.git"))
                            throw Error("path '%s' is not part of a flake (neither it nor its parent directories contain a 'flake.nix' file)", path);
                        else {
                            if (lstat(path).st_dev != device)
                                throw Error("unable to find a flake before encountering filesystem boundary at '%s'", path);
                        }
                        path = dirOf(path);
                    }
                    if (!found)
                        throw BadURL("could not find a flake.nix file");
                }

                if (!S_ISDIR(lstat(path).st_mode))
                    throw BadURL("path '%s' is not a flake (because it's not a directory)", path);

                if (!allowMissing && !pathExists(path + "/flake.nix"))
                    throw BadURL("path '%s' is not a flake (because it doesn't contain a 'flake.nix' file)", path);

                auto flakeRoot = path;
                std::string subdir;

                while (flakeRoot != "/") {
                    if (pathExists(flakeRoot + "/.git")) {
                        auto base = std::string("git+file://") + flakeRoot;

                        auto parsedURL = ParsedURL{
                            .url = base, // FIXME
                            .base = base,
                            .scheme = "git+file",
                            .authority = "",
                            .path = flakeRoot,
                            .query = decodeQuery(match[2]),
                        };

                        if (subdir != "") {
                            if (parsedURL.query.count("dir"))
                                throw Error("flake URL '%s' has an inconsistent 'dir' parameter", url);
                        }

                        if (pathExists(flakeRoot + "/.git/shallow"))
                            parsedURL.query.insert_or_assign("shallow", "1");

                        return std::make_pair(
                            FlakeRef(Input::fromURL(parsedURL, isFlake), subdir),
                            fragment);
                    }

                    subdir = std::string(baseNameOf(flakeRoot)) + (subdir.empty() ? "" : "/" + subdir);
                    flakeRoot = dirOf(flakeRoot);
                }
            }

        } else {
            if (!path.starts_with("/"))
                throw BadURL("flake reference '%s' is not an absolute path", url);
            auto query = decodeQuery(match[2]);
            path = canonPath(path + "/" + getOr(query, "dir", ""));
        }

        fetchers::Attrs attrs;
        attrs.insert_or_assign("type", "path");
        attrs.insert_or_assign("path", path);

        return std::make_pair(FlakeRef(Input::fromAttrs(std::move(attrs)), ""), fragment);
    }

    else {
        auto parsedURL = parseURL(url);
        std::string fragment;
        std::swap(fragment, parsedURL.fragment);

        // This has a special meaning for flakes and must not be passed to libfetchers.
        // Of course this means that libfetchers cannot have fetchers
        // expecting an argument `dir` 🫠
        ParsedURL urlForFetchers(parsedURL);
        urlForFetchers.query.erase("dir");

        auto input = Input::fromURL(urlForFetchers, isFlake);
        input.parent = baseDir;

        return std::make_pair(
            FlakeRef(std::move(input), getOr(parsedURL.query, "dir", "")),
            fragment);
    }
}

std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir)
{
    try {
        return parseFlakeRefWithFragment(url, baseDir);
    } catch (Error & e) {
        return {};
    }
}

FlakeRef FlakeRef::fromAttrs(const fetchers::Attrs & attrs)
{
    auto attrs2(attrs);
    attrs2.erase("dir");
    return FlakeRef(
        fetchers::Input::fromAttrs(std::move(attrs2)),
        fetchers::maybeGetStrAttr(attrs, "dir").value_or(""));
}

kj::Promise<Result<std::pair<fetchers::Tree, FlakeRef>>> FlakeRef::fetchTree(ref<Store> store) const
try {
    auto [tree, lockedInput] = TRY_AWAIT(input.fetch(store));
    co_return {std::move(tree), FlakeRef(std::move(lockedInput), subdir)};
} catch (...) {
    co_return result::current_exception();
}

std::tuple<FlakeRef, std::string, ExtendedOutputsSpec> parseFlakeRefWithFragmentAndExtendedOutputsSpec(
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(url);
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(std::string { prefix }, baseDir, allowMissing, isFlake);
    return {std::move(flakeRef), fragment, std::move(extendedOutputsSpec)};
}

}
