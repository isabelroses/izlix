#include "lix/libfetchers/attrs.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libfetchers/builtin-fetchers.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/url-parts.hh"
#include "lix/libutil/git.hh"
#include "lix/libutil/json.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/fetch-settings.hh"

#include <optional>
#include <fstream>

namespace nix::fetchers {

struct DownloadUrl
{
    std::string url;
    Headers headers;
};

static const std::set<std::string> allowedGitArchiveAttrs = {
    "host",
    "lastModified",
    "owner",
    "ref",
    "repo",
    "rev",
};

// A github, gitlab, or sourcehut host
const static std::string hostRegexS = "[a-zA-Z0-9.-]*"; // FIXME: check
std::regex hostRegex = regex::parse(hostRegexS, std::regex::ECMAScript);

struct GitArchiveInputScheme : InputScheme
{
    const std::set<std::string> & allowedAttrs() const override {
        return allowedGitArchiveAttrs;
    }

    virtual std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const = 0;

    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != schemeType()) return {};

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");

        std::optional<std::string> refOrRev;

        auto size = path.size();
        if (size == 3) {
            refOrRev = path[2];
        } else if (size > 3) {
            std::string rs;
            for (auto i = std::next(path.begin(), 2); i != path.end(); i++) {
                rs += *i;
                if (std::next(i) != path.end()) {
                    rs += "/";
                }
            }

            if (std::regex_match(rs, refRegex)) {
                refOrRev = rs;
            } else {
                throw BadURL("in URL '%s', '%s' is not a branch/tag name", url.url, rs);
            }
        } else if (size < 2)
            throw BadURL("URL '%s' is invalid", url.url);

        Attrs attrs;
        attrs.emplace("type", schemeType());
        attrs.emplace("owner", path[0]);
        attrs.emplace("repo", path[1]);

        for (auto &[name, value] : url.query) {
            if (name == "rev" || name == "ref") {
                if (refOrRev) {
                    throw BadURL("URL '%s' already contains a ref or rev", url.url);
                } else {
                    refOrRev = value;
                }
            } else if (name == "lastModified") {
                if (auto n = string2Int<uint64_t>(value)) {
                    attrs.emplace(name, *n);
                } else {
                    throw Error(
                        "Attribute 'lastModified' in URL '%s' must be an integer",
                        url.to_string()
                    );
                }
            } else {
                attrs.emplace(name, value);
            }
        }

        if (refOrRev) attrs.emplace("refOrRev", *refOrRev);

        return inputFromAttrs(attrs);
    }

    Attrs preprocessAttrs(const Attrs & attrs) const override
    {
        // Attributes can contain refOrRev and it needs to be figured out
        // which one it is (see inputFromURL for when that may happen).
        // The correct one (ref or rev) will be written into finalAttrs and
        // it needs to be mutable for that.
        Attrs finalAttrs(attrs);

        auto owner = getStrAttr(finalAttrs, "owner");
        auto repo = getStrAttr(finalAttrs, "repo");

        auto url = fmt("%s:%s/%s", schemeType(), owner, repo);
        if (auto host = maybeGetStrAttr(finalAttrs, "host")) {
            if (!std::regex_match(*host, hostRegex)) {
                throw BadURL("URL '%s' contains an invalid instance host", url);
            }
        }

        if (auto refOrRev = maybeGetStrAttr(finalAttrs, "refOrRev")) {
            finalAttrs.erase("refOrRev");
            if (std::regex_match(*refOrRev, revRegex)) {
                finalAttrs.emplace("rev", *refOrRev);
            } else if (std::regex_match(*refOrRev, refRegex)) {
                finalAttrs.emplace("ref", *refOrRev);
            } else {
                throw Error(
                    "in URL '%s', '%s' is not a commit hash or a branch/tag name",
                    url,
                    *refOrRev
                );
            }
        } else if (auto ref = maybeGetStrAttr(finalAttrs, "ref")) {
            if (!std::regex_match(*ref, refRegex)) {
                throw BadURL("URL '%s' contains an invalid branch/tag name", url);
            }
        }

        return finalAttrs;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto owner = getStrAttr(input.attrs, "owner");
        auto repo = getStrAttr(input.attrs, "repo");
        auto ref = input.getRef();
        auto rev = input.getRev();
        auto path = owner + "/" + repo;
        assert(!(ref && rev));
        if (ref) path += "/" + *ref;
        if (rev) path += "/" + rev->to_string(Base::Base16, false);
        return ParsedURL {
            .scheme = schemeType(),
            .path = path,
        };
    }

    bool hasAllInfo(const Input & input) const override
    {
        return input.getRev() && maybeGetIntAttr(input.attrs, "lastModified");
    }

    Input applyOverrides(
        const Input & _input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        auto input(_input);
        if (rev && ref)
            throw BadURL("cannot apply both a commit hash (%s) and a branch/tag name ('%s') to input '%s'",
                rev->gitRev(), *ref, input.to_string());
        if (rev) {
            input.attrs.insert_or_assign("rev", rev->gitRev());
            input.attrs.erase("ref");
        }
        if (ref) {
            input.attrs.insert_or_assign("ref", *ref);
            input.attrs.erase("rev");
        }
        return input;
    }

    std::optional<std::string> getAccessToken(const std::string & host) const
    {
        auto tokens = fetchSettings.accessTokens.get();
        if (auto token = get(tokens, host))
            return *token;
        return {};
    }

    Headers makeHeadersWithAuthTokens(const std::string & host) const
    {
        Headers headers;
        auto accessToken = getAccessToken(host);
        if (accessToken) {
            auto hdr = accessHeaderFromToken(*accessToken);
            if (hdr)
                headers.push_back(*hdr);
            else
                warn("Unrecognized access token for host '%s'", host);
        }
        return headers;
    }

    virtual kj::Promise<Result<Hash>>
    getRevFromRef(nix::ref<Store> store, const Input & input) const = 0;

    virtual DownloadUrl getDownloadUrl(const Input & input) const = 0;

    kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & _input) override
    try {
        Input input(_input);

        if (!maybeGetStrAttr(input.attrs, "ref")) input.attrs.insert_or_assign("ref", "HEAD");

        auto rev = input.getRev();
        if (!rev) rev = TRY_AWAIT(getRevFromRef(store, input));

        input.attrs.erase("ref");
        input.attrs.insert_or_assign("rev", rev->gitRev());

        auto url = getDownloadUrl(input);

        auto result =
            TRY_AWAIT(downloadTarball(store, url.url, input.getName(), true, url.headers));

        input.attrs.insert_or_assign("lastModified", uint64_t(result.lastModified));

        co_return {result.tree.storePath, input};
    } catch (...) {
        co_return result::current_exception();
    }
};

struct GitHubInputScheme : GitArchiveInputScheme
{
    std::string schemeType() const override { return "github"; }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        // Github supports PAT/OAuth2 tokens and HTTP Basic
        // Authentication.  The former simply specifies the token, the
        // latter can use the token as the password.  Only the first
        // is used here. See
        // https://developer.github.com/v3/#authentication and
        // https://docs.github.com/en/developers/apps/authorizing-oath-apps
        return std::pair<std::string, std::string>("Authorization", fmt("token %s", token));
    }

    std::string getHost(const Input & input) const
    {
        return maybeGetStrAttr(input.attrs, "host").value_or("github.com");
    }

    std::string getOwner(const Input & input) const
    {
        return getStrAttr(input.attrs, "owner");
    }

    std::string getRepo(const Input & input) const
    {
        return getStrAttr(input.attrs, "repo");
    }

    kj::Promise<Result<Hash>>
    getRevFromRef(nix::ref<Store> store, const Input & input) const override
    try {
        auto host = getHost(input);
        auto url = fmt(
            host == "github.com"
            ? "https://api.%s/repos/%s/%s/commits/%s"
            : "https://%s/api/v3/repos/%s/%s/commits/%s",
            host, getOwner(input), getRepo(input), *input.getRef());

        Headers headers = makeHeadersWithAuthTokens(host);

        auto json = json::parse(readFile(store->toRealPath(
            TRY_AWAIT(downloadFile(store, url, "source", false, headers)).storePath
        )), "a github API response");
        auto rev = Hash::parseAny(std::string { json["sha"] }, HashType::SHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        co_return rev;
    } catch (...) {
        co_return result::current_exception();
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        auto host = getHost(input);

        Headers headers = makeHeadersWithAuthTokens(host);

        // If we have no auth headers then we default to the public archive
        // urls so we do not run into rate limits.
        const auto urlFmt =
            host != "github.com"
                ? "https://%s/api/v3/repos/%s/%s/tarball/%s"
                : !getAccessToken(host)
                    ? "https://%s/%s/%s/archive/%s.tar.gz"
                    : "https://api.%s/repos/%s/%s/tarball/%s";

        const auto url = fmt(urlFmt, host, getOwner(input), getRepo(input),
            input.getRev()->to_string(Base::Base16, false));

        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto host = getHost(input);
        Input::fromURL(fmt("git+https://%s/%s/%s.git",
                host, getOwner(input), getRepo(input)))
            .applyOverrides(input.getRef(), input.getRev())
            .clone(destDir);
    }

    Headers makeHeadersWithAuthTokens(const std::string & host) const
    {
        Headers headers = GitArchiveInputScheme::makeHeadersWithAuthTokens(host);
        headers.emplace_back("X-GitHub-Api-Version", "2022-11-28");
        return headers;
    }
};

struct GitLabInputScheme : GitArchiveInputScheme
{
    std::string schemeType() const override { return "gitlab"; }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        // Gitlab supports 4 kinds of authorization, two of which are
        // relevant here: OAuth2 and PAT (Private Access Token).  The
        // user can indicate which token is used by specifying the
        // token as <TYPE>:<VALUE>, where type is "OAuth2" or "PAT".
        // If the <TYPE> is unrecognized, this will fall back to
        // treating this simply has <HDRNAME>:<HDRVAL>.  See
        // https://docs.gitlab.com/12.10/ee/api/README.html#authentication
        auto fldsplit = token.find_first_of(':');
        // n.b. C++20 would allow: if (token.starts_with("OAuth2:")) ...
        if ("OAuth2" == token.substr(0, fldsplit))
            return std::make_pair("Authorization", fmt("Bearer %s", token.substr(fldsplit+1)));
        if ("PAT" == token.substr(0, fldsplit))
            return std::make_pair("Private-token", token.substr(fldsplit+1));
        warn("Unrecognized GitLab token type %s",  token.substr(0, fldsplit));
        return std::make_pair(token.substr(0,fldsplit), token.substr(fldsplit+1));
    }

    kj::Promise<Result<Hash>>
    getRevFromRef(nix::ref<Store> store, const Input & input) const override
    try {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        // See rate limiting note below
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/commits?ref_name=%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"), *input.getRef());

        Headers headers = makeHeadersWithAuthTokens(host);

        auto json = json::parse(readFile(store->toRealPath(
            TRY_AWAIT(downloadFile(store, url, "source", false, headers)).storePath
        )), "a gitlab API response");
        if (json.is_array() && json.size() >= 1 && json[0]["id"] != nullptr) {
            auto rev = Hash::parseAny(std::string(json[0]["id"]), HashType::SHA1);
            debug("HEAD revision for '%s' is %s", url, rev.gitRev());
            co_return rev;
        } else if (json.is_array() && json.size() == 0) {
            throw Error("No commits returned by GitLab API -- does the ref really exist?");
        } else {
            throw Error("Didn't know what to do with response from GitLab: %s", json);
        }
    } catch (...) {
        co_return result::current_exception();
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        // This endpoint has a rate limit threshold that may be
        // server-specific and vary based whether the user is
        // authenticated via an accessToken or not, but the usual rate
        // is 10 reqs/sec/ip-addr.  See
        // https://docs.gitlab.com/ee/user/gitlab_com/index.html#gitlabcom-specific-rate-limits
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/archive.tar.gz?sha=%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(Base::Base16, false));

        Headers headers = makeHeadersWithAuthTokens(host);
        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        // FIXME: get username somewhere
        Input::fromURL(fmt("git+https://%s/%s/%s.git",
                host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef(), input.getRev())
            .clone(destDir);
    }
};

struct SourceHutInputScheme : GitArchiveInputScheme
{
    std::string schemeType() const override { return "sourcehut"; }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        // SourceHut supports both PAT and OAuth2. See
        // https://man.sr.ht/meta.sr.ht/oauth.md
        return std::pair<std::string, std::string>("Authorization", fmt("Bearer %s", token));
        // Note: This currently serves no purpose, as this kind of authorization
        // does not allow for downloading tarballs on sourcehut private repos.
        // Once it is implemented, however, should work as expected.
    }

    kj::Promise<Result<Hash>>
    getRevFromRef(nix::ref<Store> store, const Input & input) const override
    try {
        // TODO: In the future, when the sourcehut graphql API is implemented for mercurial
        // and with anonymous access, this method should use it instead.

        auto ref = *input.getRef();

        auto host = maybeGetStrAttr(input.attrs, "host").value_or("git.sr.ht");
        auto base_url = fmt("https://%s/%s/%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"));

        Headers headers = makeHeadersWithAuthTokens(host);

        std::string refUri;
        if (ref == "HEAD") {
            auto file = store->toRealPath(
                TRY_AWAIT(downloadFile(store, fmt("%s/HEAD", base_url), "source", false, headers))
                    .storePath
            );
            std::ifstream is(file);
            std::string line;
            getline(is, line);

            auto remoteLine = git::parseLsRemoteLine(line);
            if (!remoteLine) {
                throw BadURL("in '%d', couldn't resolve HEAD ref '%d'", input.to_string(), ref);
            }
            refUri = remoteLine->target;
        } else {
            refUri = fmt("refs/(heads|tags)/%s", ref);
        }
        std::regex refRegex = regex::parse(refUri);

        auto file = store->toRealPath(
            TRY_AWAIT(downloadFile(store, fmt("%s/info/refs", base_url), "source", false, headers))
                .storePath
        );
        std::ifstream is(file);

        std::string line;
        std::optional<std::string> id;
        while(!id && getline(is, line)) {
            auto parsedLine = git::parseLsRemoteLine(line);
            if (parsedLine && parsedLine->reference && std::regex_match(*parsedLine->reference, refRegex))
                id = parsedLine->target;
        }

        if(!id)
            throw BadURL("in '%d', couldn't find ref '%d'", input.to_string(), ref);

        auto rev = Hash::parseAny(*id, HashType::SHA1);
        debug("HEAD revision for '%s' is %s", fmt("%s/%s", base_url, ref), rev.gitRev());
        co_return rev;
    } catch (...) {
        co_return result::current_exception();
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("git.sr.ht");
        auto url = fmt("https://%s/%s/%s/archive/%s.tar.gz",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(Base::Base16, false));

        Headers headers = makeHeadersWithAuthTokens(host);
        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("git.sr.ht");
        Input::fromURL(fmt("git+https://%s/%s/%s",
                host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef(), input.getRev())
            .clone(destDir);
    }
};

std::unique_ptr<InputScheme> makeGitHubInputScheme()
{
    return std::make_unique<GitHubInputScheme>();
}

std::unique_ptr<InputScheme> makeGitLabInputScheme()
{
    return std::make_unique<GitLabInputScheme>();
}

std::unique_ptr<InputScheme> makeSourceHutInputScheme()
{
    return std::make_unique<SourceHutInputScheme>();
}
}
