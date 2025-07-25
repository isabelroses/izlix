#pragma once
///@file

#include "lix/libutil/ref.hh"
#include "lix/libstore/nar-info.hh"
#include "lix/libstore/realisation.hh"

namespace nix {

class NarInfoDiskCache
{
public:
    typedef enum { oValid, oInvalid, oUnknown } Outcome;

    virtual ~NarInfoDiskCache() { }

    virtual int createCache(const std::string & uri, const Path & storeDir,
        bool wantMassQuery, int priority) = 0;

    struct CacheInfo
    {
        int id;
        bool wantMassQuery;
        int priority;
    };

    virtual std::optional<CacheInfo> upToDateCacheExists(const std::string & uri) = 0;

    virtual std::pair<Outcome, std::shared_ptr<NarInfo>> lookupNarInfo(
        const std::string & uri, const std::string & hashPart) = 0;

    virtual void upsertNarInfo(
        const std::string & uri, const std::string & hashPart,
        std::shared_ptr<const ValidPathInfo> info) = 0;
};

/**
 * Return a singleton cache object that can be used concurrently by
 * multiple threads.
 */
ref<NarInfoDiskCache> getNarInfoDiskCache();

ref<NarInfoDiskCache> getTestNarInfoDiskCache(Path dbPath);

}
