#pragma once

#include <string>

#include "display/IDisplay.h"

namespace fdve {

// Prints a status line when something changed. Useful on a terminal and in
// the journal, and as a template for a real display driver.
class ConsoleDisplay : public IDisplay
{
public:
    void update(const ModemStatus& status) override;
    static std::string format(const ModemStatus& status);

private:
    std::string last_;
};

} // namespace fdve
