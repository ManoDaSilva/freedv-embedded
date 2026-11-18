#pragma once

#include <memory>
#include <string>

namespace fdve {

// Wraps the FreeDV backend (RADE modem + FARGAN vocoder + resampling).
// This is the only class that includes upstream headers. When upstream changes
// (RADE V2, API changes), the changes should stay inside Modem.cpp.
//
// Audio is 16 bit mono. The four entry points map onto the two sound cards:
//
//   headset mic  -> writeMic()      -> [TX thread] -> readRadioOut() -> radio
//   radio        -> writeRadioIn()  -> [RX thread] -> readSpeaker()  -> headset
//
// Only one Modem can exist per process (the backend uses global TX flags).
class Modem
{
public:
    struct Params
    {
        std::string mode = "RADEV1";
        int radioSampleRate = 48000;
        int headsetSampleRate = 48000;
        std::string callsign; // sent in the end-of-over frame, may be empty
    };

    explicit Modem(const Params& params);
    ~Modem();
    Modem(const Modem&) = delete;
    Modem& operator=(const Modem&) = delete;

    void start();
    void stop();

    // Transmit sequence: startTx() ... endTx() ... wait for txDrained() ... finishTx()
    void startTx();
    void endTx();          // stop taking mic audio, queue the end-of-over frame
    bool txDrained() const; // end-of-over generated and handed to the radio side
    void finishTx();       // back to receive

    // Audio entry points. All are non-blocking and safe to call from audio threads.
    bool writeMic(const short* samples, int count);     // false: dropped
    int radioOutAvailable() const;
    bool readRadioOut(short* samples, int count);       // false: not enough data
    bool writeRadioIn(const short* samples, int count); // false: dropped
    int speakerAvailable() const;
    bool readSpeaker(short* samples, int count);        // false: not enough data

    bool sync() const;
    int snrDb() const;
    std::string lastRxCallsign() const;
    unsigned rxCallsignCount() const; // goes up by one with every decoded callsign

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fdve
