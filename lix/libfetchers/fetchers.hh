#pragma once
///@file

#include "lix/libstore/content-address.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/canon-path.hh"
#include "lix/libstore/path.hh"
#include "lix/libfetchers/attrs.hh"
#include "lix/libutil/url.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/strings.hh"

#include <kj/async.h>
#include <memory>

namespace nix { class Store; }

namespace nix::fetchers {

MakeError(UnsupportedAttributeError, Error);

struct Tree
{
    Path actualPath;
    StorePath storePath;
};

struct InputScheme;

/**
 * The Input object is generated by a specific fetcher, based on the
 * user-supplied input attribute in the flake.nix file, and contains
 * the information that the specific fetcher needs to perform the
 * actual fetch.  The Input object is most commonly created via the
 * "fromURL()" or "fromAttrs()" static functions which are provided
 * the url or attrset specified in the flake file.
 */
struct Input
{
    friend struct InputScheme;

    std::shared_ptr<InputScheme> scheme; // note: can be null
    Attrs attrs;
    bool locked = false;
    bool direct = true;

    /**
     * path of the parent of this input, used for relative path resolution
     */
    std::optional<Path> parent;

public:
    static Input fromURL(const std::string & url, bool requireTree = true);

    static Input fromURL(const ParsedURL & url, bool requireTree = true);

    static Input fromAttrs(Attrs && attrs);

    ParsedURL toURL() const;

    std::string toURLString(const std::map<std::string, std::string> & extraQuery = {}) const;

    std::string to_string() const;

    Attrs toAttrs() const;

    /**
     * Check whether this is a "direct" input, that is, not
     * one that goes through a registry.
     */
    bool isDirect() const { return direct; }

    /**
     * Check whether this is a "locked" input, that is,
     * one that contains a commit hash or content hash.
     */
    bool isLocked() const { return locked; }

    /**
     * Check whether the input carries all necessary info required
     * for cache insertion and substitution.
     * These fields are used to uniquely identify cached trees
     * within the "tarball TTL" window without necessarily
     * indicating that the input's origin is unchanged.
     */
    bool hasAllInfo() const;

    bool operator ==(const Input & other) const;

    bool contains(const Input & other) const;

    /**
     * Fetch the input into the Nix store, returning the location in
     * the Nix store and the locked input.
     */
    kj::Promise<Result<std::pair<Tree, Input>>> fetch(ref<Store> store) const;

    Input applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    void clone(const Path & destDir) const;

    std::optional<Path> getSourcePath() const;

    /**
     * Write a file to this input, for input types that support
     * writing. Optionally commit the change (for e.g. Git inputs).
     */
    void putFile(
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const;

    std::string getName() const;

    StorePath computeStorePath(Store & store) const;

    // Convenience functions for common attributes.
    std::string getType() const;
    std::optional<Hash> getNarHash() const;
    std::optional<std::string> getRef() const;
    std::optional<Hash> getRev() const;
    std::optional<uint64_t> getRevCount() const;
    std::optional<time_t> getLastModified() const;
};

/**
 * The InputScheme represents a type of fetcher.  Each fetcher
 * registers with nix at startup time.  When processing an input for a
 * flake, each scheme is given an opportunity to "recognize" that
 * input from the url or attributes in the flake file's specification
 * and return an Input object to represent the input if it is
 * recognized.  The Input object contains the information the fetcher
 * needs to actually perform the "fetch()" when called.
 */
struct InputScheme
{
    virtual ~InputScheme()
    { }

    virtual std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const = 0;

    virtual Attrs preprocessAttrs(const Attrs & attrs) const = 0;

    // The scheme type, which is used to match attributes to a specific scheme
    virtual std::string schemeType() const = 0;

    virtual std::optional<Input> inputFromAttrs(const Attrs & attrs) const;

    virtual ParsedURL toURL(const Input & input) const;

    virtual bool hasAllInfo(const Input & input) const = 0;

    virtual Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    virtual void clone(const Input & input, const Path & destDir) const;

    virtual std::optional<Path> getSourcePath(const Input & input) const;

    virtual void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const;

    virtual kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & input) = 0;

    /*
     * By default, libfetchers considers inputs as locked if a `rev`
     * is specified. This however doesn't make any sense for `path` inputs,
     * so schemes can indicate that a `rev` on its own is not sufficient.
     */
    virtual bool isLockedByRev() const { return true; }

protected:
    // The set of allowed attributes for this specific fetcher
    virtual const std::set<std::string> & allowedAttrs() const = 0;

    void emplaceURLQueryIntoAttrs(
        const ParsedURL & parsedURL,
        Attrs & attrs,
        const StringSet & numericParams,
        const StringSet & booleanParams) const
    {
        for (auto &[name, value] : parsedURL.query) {
            if (name == "url") {
                throw BadURL(
                    "URL '%s' must not override url via query param!",
                    parsedURL.to_string()
                );
            } else if (numericParams.count(name) != 0) {
                if (auto n = string2Int<uint64_t>(value)) {
                    attrs.insert_or_assign(name, *n);
                } else {
                    throw BadURL(
                        "URL '%s' has non-numeric parameter '%s'",
                        parsedURL.to_string(),
                        name
                    );
                }
            } else if (booleanParams.count(name) != 0) {
                attrs.emplace(name, Explicit<bool> { value == "1" });
            } else {
                attrs.emplace(name, value);
            }
        }
    }
};

void registerInputScheme(std::shared_ptr<InputScheme> && fetcher);

void initLibFetchers();

struct DownloadFileResult
{
    StorePath storePath;
    std::string etag;
    std::string effectiveUrl;
    std::optional<std::string> immutableUrl;
};

kj::Promise<Result<DownloadFileResult>> downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    Headers headers = {},
    FileIngestionMethod ingestionMethod = FileIngestionMethod::Flat);

struct DownloadTarballResult
{
    Tree tree;
    time_t lastModified;
    std::optional<std::string> immutableUrl;
};

kj::Promise<Result<DownloadTarballResult>> downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers = {});

}
