#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "app/Config.h"
#include "app/Controller.h"

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int)
{
    g_stop.store(true);
}

void usage(const char* name)
{
    std::fprintf(stderr,
        "Usage: %s [-c FILE] [--check-config] [--version]\n"
        "  -c, --config FILE   configuration file (default /etc/freedv-embedded/freedv-embedded.toml)\n"
        "      --check-config  parse the configuration file and exit\n"
        "      --version       print the version and exit\n",
        name);
}

} // namespace

int main(int argc, char** argv)
{
    std::string configPath = "/etc/freedv-embedded/freedv-embedded.toml";
    bool checkOnly = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) configPath = argv[++i];
        else if (arg == "--check-config") checkOnly = true;
        else if (arg == "--version") { std::printf("freedv-embedded %s\n", FDVE_VERSION); return 0; }
        else { usage(argv[0]); return arg == "-h" || arg == "--help" ? 0 : 2; }
    }

    try
    {
        fdve::Config config = fdve::loadConfig(configPath);
        if (checkOnly)
        {
            std::printf("%s: OK\n", configPath.c_str());
            return 0;
        }

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
        return fdve::runController(config, g_stop);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "freedv-embedded: %s\n", e.what());
        return 1;
    }
}
