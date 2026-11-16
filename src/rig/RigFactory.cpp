#include <stdexcept>

#include "rig/IRig.h"
#ifdef FDVE_WITH_HAMLIB
#include "rig/HamlibRig.h"
#endif

namespace fdve {

namespace {

class NoRig : public IRig
{
public:
    bool setPtt(bool, int) override { return false; }
    bool connected() const override { return false; }
    long long frequencyHz() const override { return 0; }
};

} // namespace

std::string checkRigConfig(const RigConfig& config)
{
    if (config.type == "none") return "";
    if (config.type != "hamlib") return "unknown rig type '" + config.type + "'";
#ifdef FDVE_WITH_HAMLIB
    return HamlibRig::check(config);
#else
    return "This program was built without Hamlib. Install libhamlib-dev, run cmake again and rebuild.";
#endif
}

std::shared_ptr<IRig> createRig(const RigConfig& config)
{
    if (config.type == "none") return std::make_shared<NoRig>();
    if (config.type == "hamlib")
    {
#ifdef FDVE_WITH_HAMLIB
        return std::make_shared<HamlibRig>(config);
#else
        throw std::runtime_error("rig: " + checkRigConfig(config));
#endif
    }
    throw std::runtime_error("unknown rig type '" + config.type + "'");
}

} // namespace fdve
