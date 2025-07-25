#include "lix/libcmd/command.hh"
#include "lix/libstore/globals.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libstore/names.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libutil/hilite.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/regex.hh"
#include "search.hh"

#include <regex>
#include <fstream>

namespace nix {

std::string wrap(std::string prefix, std::string s)
{
    return concatStrings(prefix, s, ANSI_NORMAL);
}

struct CmdSearch : InstallableCommand, MixJSON
{
    std::vector<std::string> res;
    std::vector<std::string> excludeRes;

    CmdSearch()
    {
        expectArgs("regex", &res);
        addFlag(Flag {
            .longName = "exclude",
            .shortName = 'e',
            .description = "Hide packages whose attribute path, name or description contain *regex*.",
            .labels = {"regex"},
            .handler = {[this](std::string s) {
                excludeRes.push_back(s);
            }},
        });
    }

    std::string description() override
    {
        return "search for packages";
    }

    std::string doc() override
    {
        return
          #include "search.md"
          ;
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {
            "packages." + evalSettings.getCurrentSystem(),
            "legacyPackages." + evalSettings.getCurrentSystem()
        };
    }

    void run(ref<Store> store, ref<Installable> installable) override
    {
        auto const installableValue = InstallableValue::require(installable);

        settings.readOnlyMode = true;
        evalSettings.enableImportFromDerivation.setDefault(false);

        // Recommend "^" here instead of ".*" due to differences in resulting highlighting
        if (res.empty())
            throw UsageError("Must provide at least one regex! To match all packages, use '%s'.", "nix search <installable> ^");

        std::vector<std::regex> regexes;
        std::vector<std::regex> excludeRegexes;
        regexes.reserve(res.size());
        excludeRegexes.reserve(excludeRes.size());

        for (auto & re : res)
            regexes.push_back(nix::regex::parse(re, std::regex::extended | std::regex::icase));

        for (auto & re : excludeRes)
            excludeRegexes.emplace_back(nix::regex::parse(re, std::regex::extended | std::regex::icase));

        auto evaluator = getEvaluator();
        auto state = evaluator->begin(aio());

        std::optional<JSON> jsonOut;
        if (json) jsonOut = JSON::object();

        uint64_t results = 0;

        std::function<void(eval_cache::AttrCursor & cursor, const std::vector<std::string> & attrPath, bool initialRecurse)> visit;

        visit = [&](eval_cache::AttrCursor & cursor, const std::vector<std::string> & attrPath, bool initialRecurse)
        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));
            try {
                auto recurse = [&]()
                {
                    for (const auto & attr : cursor.getAttrs(*state)) {
                        auto cursor2 = cursor.getAttr(*state, attr);
                        auto attrPath2(attrPath);
                        attrPath2.emplace_back(attr);
                        visit(*cursor2, attrPath2, false);
                    }
                };

                if (cursor.isDerivation(*state)) {
                    DrvName name(cursor.getAttr(*state, "name")->getString(*state));

                    auto aMeta = cursor.maybeGetAttr(*state, "meta");
                    auto aDescription = aMeta ? aMeta->maybeGetAttr(*state, "description") : nullptr;
                    auto description = aDescription ? aDescription->getString(*state) : "";
                    std::replace(description.begin(), description.end(), '\n', ' ');
                    auto attrPath2 = concatStringsSep(".", attrPath);

                    std::vector<std::smatch> attrPathMatches;
                    std::vector<std::smatch> descriptionMatches;
                    std::vector<std::smatch> nameMatches;
                    bool found = false;

                    for (auto & regex : excludeRegexes) {
                        if (
                            std::regex_search(attrPath2, regex)
                            || std::regex_search(name.name, regex)
                            || std::regex_search(description, regex))
                            return;
                    }

                    for (auto & regex : regexes) {
                        found = false;
                        auto addAll = [&found](std::sregex_iterator it, std::vector<std::smatch> & vec) {
                            const auto end = std::sregex_iterator();
                            while (it != end) {
                                vec.push_back(*it++);
                                found = true;
                            }
                        };

                        addAll(std::sregex_iterator(attrPath2.begin(), attrPath2.end(), regex), attrPathMatches);
                        addAll(std::sregex_iterator(name.name.begin(), name.name.end(), regex), nameMatches);
                        addAll(std::sregex_iterator(description.begin(), description.end(), regex), descriptionMatches);

                        if (!found)
                            break;
                    }

                    if (found)
                    {
                        results++;
                        if (json) {
                            (*jsonOut)[attrPath2] = {
                                {"pname", name.name},
                                {"version", name.version},
                                {"description", description},
                            };
                        } else {
                            if (results > 1) logger->cout("");
                            logger->cout(
                                "* %s%s",
                                wrap("\e[0;1m", hiliteMatches(attrPath2, attrPathMatches, ANSI_GREEN, "\e[0;1m")),
                                name.version != "" ? " (" + name.version + ")" : "");
                            if (description != "")
                                logger->cout(
                                    "  %s", hiliteMatches(description, descriptionMatches, ANSI_GREEN, ANSI_NORMAL));
                        }
                    }
                }

                else if (
                    attrPath.size() == 0
                    || (attrPath[0] == "legacyPackages" && attrPath.size() <= 2)
                    || (attrPath[0] == "packages" && attrPath.size() <= 2))
                    recurse();

                else if (initialRecurse)
                    recurse();

                else if (attrPath[0] == "legacyPackages" && attrPath.size() > 2) {
                    auto attr = cursor.maybeGetAttr(*state, "recurseForDerivations");
                    if (attr && attr->getBool(*state))
                        recurse();
                }

            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    throw;
            }
        };

        for (auto & cursor : installableValue->getCursors(*state))
            visit(*cursor, cursor->getAttrPath(*state), true);

        if (json)
            logger->cout("%s", *jsonOut);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

void registerNixSearch()
{
    registerCommand<CmdSearch>("search");
}

}
