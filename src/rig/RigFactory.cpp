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

std::shared_ptr<IRig> createRig(const RigConfig& config)
{
    if (config.type == "none") return std::make_shared<NoRig>();
    if (config.type == "hamlib")
    {
#ifdef FDVE_WITH_HAMLIB
        return std::make_shared<HamlibRig>(config);
#else
        throw std::runtime_error("rig type \"hamlib\" is not available: this program was built without Hamlib "
                                 "(install libhamlib-dev and rebuild)");
#endif
    }
    throw std::runtime_error("unknown rig type '" + config.type + "'");
}

} // namespace fdve
