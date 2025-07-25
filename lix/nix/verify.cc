#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/exit.hh"
#include "verify.hh"

#include <atomic>
#include <functional>

namespace nix {

struct CmdVerify : StorePathsCommand
{
    bool noContents = false;
    bool noTrust = false;
    Strings substituterUris;
    size_t sigsNeeded = 0;

    CmdVerify()
    {
        addFlag({
            .longName = "no-contents",
            .description = "Do not verify the contents of each store path.",
            .handler = {&noContents, true},
        });

        addFlag({
            .longName = "no-trust",
            .description = "Do not verify whether each store path is trusted.",
            .handler = {&noTrust, true},
        });

        addFlag({
            .longName = "substituter",
            .shortName = 's',
            .description = "Use signatures from the specified store.",
            .labels = {"store-uri"},
            .handler = {[&](std::string s) { substituterUris.push_back(s); }}
        });

        addFlag({
            .longName = "sigs-needed",
            .shortName = 'n',
            .description = "Require that each path is signed by at least *n* different keys.",
            .labels = {"n"},
            .handler = {&sigsNeeded}
        });
    }

    std::string description() override
    {
        return "verify the integrity of store paths";
    }

    std::string doc() override
    {
        return
          #include "verify.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        std::vector<ref<Store>> substituters;
        for (auto & s : substituterUris)
            substituters.push_back(aio().blockOn(openStore(s)));

        auto publicKeys = getDefaultPublicKeys();

        Activity act(*logger, actVerifyPaths);

        std::atomic<size_t> done{0};
        std::atomic<size_t> untrusted{0};
        std::atomic<size_t> corrupted{0};
        std::atomic<size_t> failed{0};
        std::atomic<size_t> active{0};

        auto update = [&]() {
            act.progress(done, storePaths.size(), active, failed);
        };

        ThreadPool pool{"Verify pool"};

        auto doPath = [&](AsyncIoRoot & aio, const StorePath & storePath) {
            try {
                MaintainCount<std::atomic<size_t>> mcActive(active);
                update();

                auto info = aio.blockOn(store->queryPathInfo(storePath));

                // Note: info->path can be different from storePath
                // for binary cache stores when using --all (since we
                // can't enumerate names efficiently).
                Activity act2(*logger, lvlInfo, actUnknown, fmt("checking '%s'", store->printStorePath(info->path)));

                if (!noContents) {

                    auto hashSink = HashSink(info->narHash.type);

                    aio.blockOn(aio.blockOn(store->narFromPath(info->path))->drainInto(hashSink));

                    auto hash = hashSink.finish();

                    if (hash.first != info->narHash) {
                        corrupted++;
                        act2.result(resCorruptedPath, store->printStorePath(info->path));
                        printError("path '%s' was modified! expected hash '%s', got '%s'",
                            store->printStorePath(info->path),
                            info->narHash.to_string(Base::SRI, true),
                            hash.first.to_string(Base::SRI, true));
                    }
                }

                if (!noTrust) {

                    bool good = false;

                    if (info->ultimate && !sigsNeeded)
                        good = true;

                    else {

                        StringSet sigsSeen;
                        size_t actualSigsNeeded = std::max(sigsNeeded, (size_t) 1);
                        size_t validSigs = 0;

                        auto doSigs = [&](StringSet sigs) {
                            for (auto sig : sigs) {
                                if (!sigsSeen.insert(sig).second) continue;
                                if (validSigs < ValidPathInfo::maxSigs && info->checkSignature(*store, publicKeys, sig))
                                    validSigs++;
                            }
                        };

                        if (info->isContentAddressed(*store)) validSigs = ValidPathInfo::maxSigs;

                        doSigs(info->sigs);

                        for (auto & store2 : substituters) {
                            if (validSigs >= actualSigsNeeded) break;
                            try {
                                auto info2 = aio.blockOn(store2->queryPathInfo(info->path));
                                if (info2->isContentAddressed(*store)) {
                                    validSigs = ValidPathInfo::maxSigs;
                                }
                                doSigs(info2->sigs);
                            } catch (InvalidPath &) {
                            } catch (Error & e) {
                                logError(e.info());
                            }
                        }

                        if (validSigs >= actualSigsNeeded)
                            good = true;
                    }

                    if (!good) {
                        untrusted++;
                        act2.result(resUntrustedPath, store->printStorePath(info->path));
                        printError("path '%s' is untrusted", store->printStorePath(info->path));
                    }

                }

                done++;

            } catch (Error & e) {
                logError(e.info());
                failed++;
            }

            update();
        };

        for (auto & storePath : storePaths)
            pool.enqueueWithAio(std::bind(doPath, std::placeholders::_1, storePath));

        pool.process();

        throw Exit(
            (corrupted ? 1 : 0) |
            (untrusted ? 2 : 0) |
            (failed ? 4 : 0));
    }
};

void registerNixStoreVerify()
{
    registerCommand2<CmdVerify>({"store", "verify"});
}

}
