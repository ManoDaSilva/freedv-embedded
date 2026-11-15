#include "modem/Modem.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "MinimalRealTimeHelper.h"
#include "MinimalTxRxThread.h"
#include "paCallbackData.h"
#include "pipeline_defines.h"
#include "rade_api.h"
#include "rade_text.h"

extern "C"
{
#include "fargan.h"
#include "fargan_config.h"
#include "lpcnet.h"
}

// The backend reads these as globals and expects the application to define
// them. g_eoo_enqueued is the exception: the backend defines it.
std::atomic<bool> g_tx{false};
std::atomic<bool> endingTx{false};
extern std::atomic<bool> g_eoo_enqueued;

// Debug hooks of the backend: when set, the RADE steps dump vocoder features
// to these files. Left empty, which disables the dumps.
std::string utRxFeatureFile;
std::string utTxFeatureFile;

namespace fdve {

namespace {

int radeFlagsForMode(const std::string& mode)
{
    const int base = RADE_USE_C_ENCODER | RADE_USE_C_DECODER | RADE_VERBOSE_0;
    if (mode == "RADEV1") return base;
    // RADE V2 exists in rade_c behind RADE_MODE_V2, but the backend pipeline
    // steps are only exercised upstream with V1. Add the mapping here once
    // freedv-backend supports it.
    throw std::runtime_error("unsupported modem mode '" + mode + "' (supported: RADEV1)");
}

int fifoSamples(int sampleRate)
{
    return FIFO_SIZE * sampleRate / MS_TO_SEC;
}

} // namespace

struct Modem::Impl
{
    Params params;
    struct rade* rade = nullptr;
    rade_text_t radeText = nullptr;
    FARGANState fargan;
    LPCNetEncState* lpcnetEnc = nullptr;
    paCallBackData fifos;
    std::unique_ptr<MinimalTxRxThread> txThread;
    std::unique_ptr<MinimalTxRxThread> rxThread;
    bool running = false;

    mutable std::mutex callsignMutex;
    std::string rxCallsign;

    static void onTextRx(rade_text_t, const char* text, int length, void* state)
    {
        auto self = static_cast<Impl*>(state);
        if (text == nullptr || length <= 0) return;
        std::lock_guard<std::mutex> lock(self->callsignMutex);
        self->rxCallsign.assign(text, length);
    }
};

Modem::Modem(const Params& params) : impl_(std::make_unique<Impl>())
{
    impl_->params = params;
    const int flags = radeFlagsForMode(params.mode);

    rade_initialize();
    char modelFile[1] = {0}; // weights are compiled into librade
    impl_->rade = rade_open(modelFile, flags);
    if (impl_->rade == nullptr) throw std::runtime_error("rade_open failed");

    impl_->radeText = rade_text_create();
    rade_text_set_rx_callback(impl_->radeText, &Impl::onTextRx, impl_.get());

    if (!params.callsign.empty())
    {
        std::vector<float> eoo(rade_n_eoo_bits(impl_->rade));
        if (!eoo.empty())
        {
            rade_text_generate_tx_string(impl_->radeText, params.callsign.c_str(),
                                         params.callsign.size(), eoo.data(), eoo.size());
            rade_tx_set_eoo_bits(impl_->rade, eoo.data());
        }
    }

    float zeros[320] = {0};
    float features[5 * NB_TOTAL_FEATURES] = {0};
    fargan_init(&impl_->fargan);
    fargan_cont(&impl_->fargan, zeros, features);
    impl_->lpcnetEnc = lpcnet_encoder_create();

    // Naming follows the backend: fifo1 is the transmit path, fifo2 the receive path.
    impl_->fifos.infifo1 = new GenericFIFO<short>(fifoSamples(params.headsetSampleRate));
    impl_->fifos.outfifo1 = new GenericFIFO<short>(fifoSamples(params.radioSampleRate));
    impl_->fifos.infifo2 = new GenericFIFO<short>(fifoSamples(params.radioSampleRate));
    impl_->fifos.outfifo2 = new GenericFIFO<short>(fifoSamples(params.headsetSampleRate));

    auto rtHelper = std::make_shared<MinimalRealtimeHelper>();
    impl_->txThread = std::make_unique<MinimalTxRxThread>(
        true, params.headsetSampleRate, params.radioSampleRate, rtHelper, impl_->rade,
        impl_->lpcnetEnc, &impl_->fargan, impl_->radeText, &impl_->fifos);
    impl_->rxThread = std::make_unique<MinimalTxRxThread>(
        false, params.radioSampleRate, params.headsetSampleRate, rtHelper, impl_->rade,
        impl_->lpcnetEnc, &impl_->fargan, impl_->radeText, &impl_->fifos);
}

Modem::~Modem()
{
    stop();
    impl_->txThread.reset();
    impl_->rxThread.reset();
    delete impl_->fifos.infifo1;
    delete impl_->fifos.outfifo1;
    delete impl_->fifos.infifo2;
    delete impl_->fifos.outfifo2;
    lpcnet_encoder_destroy(impl_->lpcnetEnc);
    rade_text_destroy(impl_->radeText);
    rade_close(impl_->rade);
    rade_finalize();
}

void Modem::start()
{
    if (impl_->running) return;
    g_tx.store(false);
    endingTx.store(false);
    impl_->txThread->start();
    impl_->rxThread->start();
    impl_->txThread->waitForReady();
    impl_->rxThread->waitForReady();
    impl_->txThread->signalToStart();
    impl_->rxThread->signalToStart();
    impl_->running = true;
}

void Modem::stop()
{
    if (!impl_->running) return;
    impl_->txThread->stop();
    impl_->rxThread->stop();
    g_tx.store(false);
    endingTx.store(false);
    impl_->running = false;
}

void Modem::startTx()
{
    g_eoo_enqueued.store(false);
    endingTx.store(false);
    g_tx.store(true);
}

void Modem::endTx()
{
    endingTx.store(true);
}

bool Modem::txDrained() const
{
    return g_eoo_enqueued.load() && impl_->fifos.outfifo1->numUsed() == 0;
}

void Modem::finishTx()
{
    g_tx.store(false);
    endingTx.store(false);
}

bool Modem::writeMic(const short* samples, int count)
{
    // Mic audio only matters while transmitting. Once the over is ending, the
    // TX thread must see the FIFO run empty before it sends the end-of-over frame.
    if (!g_tx.load() || endingTx.load()) return false;
    return impl_->fifos.infifo1->write(const_cast<short*>(samples), count) == 0;
}

int Modem::radioOutAvailable() const
{
    return impl_->fifos.outfifo1->numUsed();
}

bool Modem::readRadioOut(short* samples, int count)
{
    return impl_->fifos.outfifo1->read(samples, count) == 0;
}

bool Modem::writeRadioIn(const short* samples, int count)
{
    if (g_tx.load()) return false;
    return impl_->fifos.infifo2->write(const_cast<short*>(samples), count) == 0;
}

int Modem::speakerAvailable() const
{
    return impl_->fifos.outfifo2->numUsed();
}

bool Modem::readSpeaker(short* samples, int count)
{
    return impl_->fifos.outfifo2->read(samples, count) == 0;
}

bool Modem::sync() const
{
    return impl_->rxThread->getSync() != 0;
}

int Modem::snrDb() const
{
    return impl_->rxThread->getSnr();
}

std::string Modem::lastRxCallsign() const
{
    std::lock_guard<std::mutex> lock(impl_->callsignMutex);
    return impl_->rxCallsign;
}

} // namespace fdve
