#include "lix/libstore/profiles.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libexpr/eval-settings.hh" // for defexpr
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/users.hh"
#include "nix-channel.hh"

#include <fcntl.h>
#include <regex>
#include <pwd.h>

namespace nix {

typedef std::map<std::string, std::string> Channels;

static Channels channels;
static Path channelsList;

// Reads the list of channels.
static void readChannels()
{
    if (!pathExists(channelsList)) return;
    auto channelsFile = readFile(channelsList);

    for (const auto & line : tokenizeString<std::vector<std::string>>(channelsFile, "\n")) {
        chomp(line);
        if (std::regex_search(line, regex::parse("^\\s*\\#")))
            continue;
        auto split = tokenizeString<std::vector<std::string>>(line, " ");
        auto url = std::regex_replace(split[0], regex::parse("/*$"), "");
        auto name = split.size() > 1 ? split[1] : std::string(baseNameOf(url));
        channels[name] = url;
    }
}

// Writes the list of channels.
static void writeChannels()
{
    auto channelsFD = AutoCloseFD{open(channelsList.c_str(), O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, 0644)};
    if (!channelsFD)
        throw SysError("opening '%1%' for writing", channelsList);
    for (const auto & channel : channels)
        writeFull(channelsFD.get(), channel.second + " " + channel.first + "\n");
}

// Adds a channel.
static void addChannel(const std::string & url, const std::string & name)
{
    if (!regex_search(url, regex::parse("^(file|http|https)://")))
        throw Error("invalid channel URL '%1%'", url);
    if (!regex_search(name, regex::parse("^[a-zA-Z0-9_][a-zA-Z0-9_\\.-]*$")))
        throw Error("invalid channel identifier '%1%'", name);
    readChannels();
    channels[name] = url;
    writeChannels();
}

static Path profile;

// Remove a channel.
static void removeChannel(const std::string & name)
{
    readChannels();
    channels.erase(name);
    writeChannels();

    runProgram(settings.nixBinDir + "/nix-env", true, { "--profile", profile, "--uninstall", name });
}

static Path nixDefExpr;

// Fetch Nix expressions and binary cache URLs from the subscribed channels.
static void update(AsyncIoRoot & aio, const StringSet & channelNames)
{
    readChannels();

    auto store = aio.blockOn(openStore());

    auto [fd, unpackChannelPath] = createTempFile();
    writeFull(fd.get(),
        #include "unpack-channel.nix.gen.hh"
        );
    fd.reset();
    AutoDelete del(unpackChannelPath, false);

    // Download each channel.
    Strings exprs;
    for (const auto & channel : channels) {
        auto name = channel.first;
        auto url = channel.second;

        // If the URL contains a version number, append it to the name
        // attribute (so that "nix-env -q" on the channels profile
        // shows something useful).
        auto cname = name;
        std::smatch match;
        auto urlBase = std::string(baseNameOf(url));
        if (std::regex_search(urlBase, match, regex::parse("(-\\d.*)$")))
            cname = cname + match.str(1);

        std::string extraAttrs;

        if (!(channelNames.empty() || channelNames.count(name))) {
            // no need to update this channel, reuse the existing store path
            Path symlink = profile + "/" + name;
            Path storepath = dirOf(readLink(symlink));
            exprs.push_back("f: rec { name = \"" + cname + "\"; type = \"derivation\"; outputs = [\"out\"]; system = \"builtin\"; outPath = builtins.storePath \"" + storepath + "\"; out = { inherit outPath; };}");
        } else {
            // We want to download the url to a file to see if it's a tarball while also checking if we
            // got redirected in the process, so that we can grab the various parts of a nix channel
            // definition from a consistent location if the redirect changes mid-download.
            auto result = aio.blockOn(fetchers::downloadFile(
                store,
                url,
                std::string(baseNameOf(url)),
                false
            ));
            auto filename = store->toRealPath(result.storePath);
            url = result.effectiveUrl;

            bool unpacked = false;
            if (std::regex_search(filename, regex::parse("\\.tar\\.(gz|bz2|xz)$"))) {
                runProgram(settings.nixBinDir + "/nix-build", false, { "--no-out-link", "--expr", "import " + unpackChannelPath +
                            "{ name = \"" + cname + "\"; channelName = \"" + name + "\"; src = builtins.storePath \"" + filename + "\"; }" });
                unpacked = true;
            }

            if (!unpacked) {
                // Download the channel tarball.
                std::optional<fetchers::DownloadFileResult> exprs;
                try {
                    exprs = aio.blockOn(fetchers::downloadFile(
                        store, url + "/nixexprs.tar.xz", "nixexprs.tar.xz", false
                    ));
                } catch (FileTransferError & e) {
                    exprs = aio.blockOn(fetchers::downloadFile(
                        store, url + "/nixexprs.tar.bz2", "nixexprs.tar.bz2", false
                    ));
                }
                filename = store->toRealPath(exprs->storePath);
            }
            // Regardless of where it came from, add the expression representing this channel to accumulated expression
            exprs.push_back("f: f { name = \"" + cname + "\"; channelName = \"" + name + "\"; src = builtins.storePath \"" + filename + "\"; " + extraAttrs + " }");
        }
    }

    // Unpack the channel tarballs into the Nix store and install them
    // into the channels profile.
    std::cerr << "unpacking " << exprs.size() << " channels...\n";
    Strings envArgs{ "--profile", profile, "--file", unpackChannelPath, "--install", "--remove-all", "--from-expression" };
    for (auto & expr : exprs)
        envArgs.push_back(std::move(expr));
    envArgs.push_back("--quiet");
    runProgram(settings.nixBinDir + "/nix-env", false, envArgs);

    // Make the channels appear in nix-env.
    struct stat st;
    if (lstat(nixDefExpr.c_str(), &st) == 0) {
        if (S_ISLNK(st.st_mode))
            // old-skool ~/.nix-defexpr
            if (unlink(nixDefExpr.c_str()) == -1)
                throw SysError("unlinking %1%", nixDefExpr);
    } else if (errno != ENOENT) {
        throw SysError("getting status of %1%", nixDefExpr);
    }
    createDirs(nixDefExpr);
    auto channelLink = nixDefExpr + "/channels";
    replaceSymlink(profile, channelLink);
}

static int main_nix_channel(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        // Figure out the name of the `.nix-channels' file to use
        auto home = getHome();
        channelsList = settings.useXDGBaseDirectories ? createNixStateDir() + "/channels" : home + "/.nix-channels";
        nixDefExpr = getNixDefExpr();

        // Figure out the name of the channels profile.
        profile = profilesDir() +  "/channels";
        createDirs(dirOf(profile));

        enum {
            cNone,
            cAdd,
            cRemove,
            cList,
            cUpdate,
            cListGenerations,
            cRollback
        } cmd = cNone;
        std::vector<std::string> args;
        LegacyArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help") {
                showManPage("nix-channel");
            } else if (*arg == "--version") {
                printVersion("nix-channel");
            } else if (*arg == "--add") {
                cmd = cAdd;
            } else if (*arg == "--remove") {
                cmd = cRemove;
            } else if (*arg == "--list") {
                cmd = cList;
            } else if (*arg == "--update") {
                cmd = cUpdate;
            } else if (*arg == "--list-generations") {
                cmd = cListGenerations;
            } else if (*arg == "--rollback") {
                cmd = cRollback;
            } else {
                if ((*arg).starts_with("-"))
                    throw UsageError("unsupported argument '%s'", *arg);
                args.push_back(std::move(*arg));
            }
            return true;
        }).parseCmdline(argv);

        switch (cmd) {
            case cNone:
                throw UsageError("no command specified");
            case cAdd:
                if (args.size() < 1 || args.size() > 2)
                    throw UsageError("'--add' requires one or two arguments");
                {
                auto url = args[0];
                std::string name;
                if (args.size() == 2) {
                    name = args[1];
                } else {
                    name = baseNameOf(url);
                    name = std::regex_replace(name, regex::parse("-unstable$"), "");
                    name = std::regex_replace(name, regex::parse("-stable$"), "");
                }
                addChannel(url, name);
                }
                break;
            case cRemove:
                if (args.size() != 1)
                    throw UsageError("'--remove' requires one argument");
                removeChannel(args[0]);
                break;
            case cList:
                if (!args.empty())
                    throw UsageError("'--list' expects no arguments");
                readChannels();
                for (const auto & channel : channels)
                    std::cout << channel.first << ' ' << channel.second << '\n';
                break;
            case cUpdate:
                update(aio, StringSet(args.begin(), args.end()));
                break;
            case cListGenerations:
                if (!args.empty())
                    throw UsageError("'--list-generations' expects no arguments");
                std::cout << runProgram(settings.nixBinDir + "/nix-env", false, {"--profile", profile, "--list-generations"}) << std::flush;
                break;
            case cRollback:
                if (args.size() > 1)
                    throw UsageError("'--rollback' has at most one argument");
                Strings envArgs{"--profile", profile};
                if (args.size() == 1) {
                    envArgs.push_back("--switch-generation");
                    envArgs.push_back(args[0]);
                } else {
                    envArgs.push_back("--rollback");
                }
                runProgram(settings.nixBinDir + "/nix-env", false, envArgs);
                break;
        }

        return 0;
    }
}

void registerLegacyNixChannel() {
    LegacyCommandRegistry::add("nix-channel", main_nix_channel);
}

}
