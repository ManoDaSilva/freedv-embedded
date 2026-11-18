// The tests construct Reporter with their own sink. This stands in for the
// network sink so the test does not link the backend's reporting clients.
#include <stdexcept>

#include "reporting/Reporter.h"

namespace fdve {

std::unique_ptr<IReportSink> createBackendSink(const ReporterSettings&)
{
    throw std::logic_error("the network sink is not part of the tests");
}

} // namespace fdve
