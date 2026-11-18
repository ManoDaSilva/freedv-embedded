#include "reporting/ReportPolicy.h"

#include <algorithm>

namespace fdve {

using std::chrono::seconds;

void ReportPolicy::step(const ReportInput& in, std::chrono::steady_clock::time_point now)
{
    if (first_)
    {
        first_ = false;
        callsignCount_ = in.rxCallsignCount; // a callsign heard before reporting started is not reported
        analog_ = in.analog;
        transmitting_ = in.transmitting;
        sink_.analog(in.analog);
        sink_.transmit(mode_, in.transmitting);
        sink_.frequency(in.frequencyHz);
        sentFrequency_ = in.frequencyHz;
        lastFrequency_ = now;
        lastSync_ = now;
        return;
    }

    if (in.frequencyHz != sentFrequency_ && now - lastFrequency_ >= seconds(1))
    {
        sink_.frequency(in.frequencyHz);
        sentFrequency_ = in.frequencyHz;
        lastFrequency_ = now;
    }

    if (in.analog != analog_)
    {
        analog_ = in.analog;
        sink_.analog(in.analog);
    }

    if (in.transmitting != transmitting_)
    {
        transmitting_ = in.transmitting;
        sink_.transmit(mode_, in.transmitting);
    }

    const int snr = std::clamp(in.snrDb, -128, 127);
    if (in.rxCallsignCount != callsignCount_)
    {
        callsignCount_ = in.rxCallsignCount;
        if (!in.rxCallsign.empty())
        {
            sink_.received(in.rxCallsign, mode_, in.frequencyHz, snr, in.frequencyHz > 0);
            lastSync_ = now;
        }
    }
    else if (in.sync && !in.transmitting && !in.analog && now - lastSync_ >= seconds(1))
    {
        sink_.received("", mode_, in.frequencyHz, snr, false);
        lastSync_ = now;
    }

    sink_.tick(now);
}

} // namespace fdve
