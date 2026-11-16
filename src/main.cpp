#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "app/Config.h"
#include "app/Controller.h"
#include "util/Log.h"

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
        else if (arg == "--version") { std::printf("freedv-embedded %s, by Manoel - ON6RF\n", FDVE_VERSION); return 0; }
        else { usage(argv[0]); return arg == "-h" || arg == "--help" ? 0 : 2; }
    }

    try
    {
        fdve::Config config = fdve::loadConfig(configPath);
        fdve::setLogLevel(config.logLevel);
        if (checkOnly)
        {
            std::printf("%s: OK\n", configPath.c_str());
            return 0;
        }

        FDVE_INFO("freedv-embedded %s, by Manoel - ON6RF", FDVE_VERSION);

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
        switch (fdve::runController(config, configPath, g_stop))
        {
            case fdve::RunResult::Stopped: return 0;
            case fdve::RunResult::Failed: return 1;
            case fdve::RunResult::Restart: break;
            case fdve::RunResult::Shutdown:
            {
                // Everything is closed and the radio is unkeyed at this point.
                const std::string command = config.menu.shutdownCommand.empty()
                    ? "systemctl poweroff || sudo -n systemctl poweroff"
                    : config.menu.shutdownCommand;
                FDVE_INFO("shutting the computer down: %s", command.c_str());
                const int status = std::system(command.c_str());
                if (status == 0) return 0;
                std::fprintf(stderr, "freedv-embedded: the shutdown command failed. See the README, "
                                     "\"Shut down\", for the permission it needs.\n");
                return 1;
            }
        }
        // Settings were changed from the menu. Start again from scratch as a
        // fresh process image, so every device is reopened with the new file.
        FDVE_INFO("restarting with the new settings");
        execv("/proc/self/exe", argv);
        std::fprintf(stderr, "freedv-embedded: restart failed: %s\n", std::strerror(errno));
        return 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "freedv-embedded: %s\n", e.what());
        return 1;
    }
}
