#pragma once
///@file

#include "lix/libutil/config.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libutil/comparator.hh"
#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/result.hh"

#include <kj/async.h>
#include <variant>

namespace nix {

class Store;

/**
 * An opaque derived path.
 *
 * Opaque derived paths are just store paths, and fully evaluated. They
 * cannot be simplified further. Since they are opaque, they cannot be
 * built, but they can fetched.
 */
struct DerivedPathOpaque {
    StorePath path;

    std::string to_string(const Store & store) const;
    static DerivedPathOpaque parse(const Store & store, std::string_view);
    kj::Promise<Result<JSON>> toJSON(const Store & store) const;

    GENERATE_CMP(DerivedPathOpaque, me->path);
};

struct SingleDerivedPath;

/**
 * A single derived path that is built from a derivation
 *
 * Built derived paths are pair of a derivation and an output name. They are
 * evaluated by building the derivation, and then taking the resulting output
 * path of the given output name.
 */
struct SingleDerivedPathBuilt {
    DerivedPathOpaque drvPath;
    OutputName output;

    DECLARE_CMP(SingleDerivedPathBuilt);
};

namespace derived_path::detail {
using SingleDerivedPathRaw = std::variant<
    DerivedPathOpaque,
    SingleDerivedPathBuilt
>;
}

/**
 * A "derived path" is a very simple sort of expression (not a Nix
 * language expression! But an expression in a the general sense) that
 * evaluates to (concrete) store path. It is either:
 *
 * - opaque, in which case it is just a concrete store path with
 *   possibly no known derivation
 *
 * - built, in which case it is a pair of a derivation path and an
 *   output name.
 */
struct SingleDerivedPath : derived_path::detail::SingleDerivedPathRaw {
    using Raw = derived_path::detail::SingleDerivedPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = SingleDerivedPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }
};

static inline DerivedPathOpaque makeConstantStorePath(StorePath drvPath)
{
    return SingleDerivedPath::Opaque { std::move(drvPath) };
}

/**
 * A set of derived paths that are built from a derivation
 *
 * Built derived paths are pair of a derivation and some output names.
 * They are evaluated by building the derivation, and then replacing the
 * output names with the resulting outputs.
 *
 * Note that does mean a derived store paths evaluates to multiple
 * opaque paths, which is sort of icky as expressions are supposed to
 * evaluate to single values. Perhaps this should have just a single
 * output name.
 */
struct DerivedPathBuilt {
    DerivedPathOpaque drvPath;
    OutputsSpec outputs;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const Store & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const Store & store) const;
    /**
     * The caller splits on the separator, so it works for both variants.
     */
    static DerivedPathBuilt parse(const Store & store, DerivedPathOpaque, std::string_view);
    kj::Promise<Result<JSON>> toJSON(Store & store) const;

    DECLARE_CMP(DerivedPathBuilt);
};

namespace derived_path::detail {
using DerivedPathRaw = std::variant<
    DerivedPathOpaque,
    DerivedPathBuilt
>;
}

/**
 * A "derived path" is a very simple sort of expression that evaluates
 * to one or more (concrete) store paths. It is either:
 *
 * - opaque, in which case it is just a single concrete store path with
 *   possibly no known derivation
 *
 * - built, in which case it is a pair of a derivation path and some
 *   output names.
 */
struct DerivedPath : derived_path::detail::DerivedPathRaw {
    using Raw = derived_path::detail::DerivedPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = DerivedPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    /**
     * Get the store path this is ultimately derived from (by realising
     * and projecting outputs).
     *
     * Note that this is *not* a property of the store object being
     * referred to, but just of this path --- how we happened to be
     * referring to that store object. In other words, this means this
     * function breaks "referential transparency". It should therefore
     * be used only with great care.
     */
    const StorePath & getBaseStorePath() const;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const Store & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const Store & store) const;
    /**
     * Uses `!` as the separator
     */
    static DerivedPath parseLegacy(const Store & store, std::string_view);

    /**
     * Convert a `SingleDerivedPath` to a `DerivedPath`.
     */
    static DerivedPath fromSingle(const SingleDerivedPath &);
};

typedef std::vector<DerivedPath> DerivedPaths;

}
