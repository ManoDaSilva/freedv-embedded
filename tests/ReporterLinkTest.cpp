// Links the real network sink the way the program does, with the backend's
// utility library pulled in first by other parts. Catches link order
// problems that the other tests, which use a stand-in sink, cannot see.
// It does not connect anywhere unless run with --connect.

#include <cstdio>
#include <cstring>

#include "reporting/Reporter.h"

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--connect") == 0)
    {
        fdve::ReporterSettings settings;
        settings.callsign = "ZZ0ZZZ";
        settings.gridSquare = "JO20";
        settings.software = "freedv-embedded test";
        settings.mode = "RADEV1";
        auto sink = fdve::createBackendSink(settings);
        std::printf("status: %s\n", sink->status().c_str());
    }
    std::printf("PASS\n");
    return 0;
}
