#include "display/Ssd1306Display.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "display/StatusScreen.h"
#include "util/Log.h"

namespace fdve {

Ssd1306Display::Ssd1306Display(const DisplayConfig& config)
{
    fd_ = open(config.i2cDevice.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
        throw std::runtime_error("ssd1306: cannot open " + config.i2cDevice + ": " + std::strerror(errno));
    if (ioctl(fd_, I2C_SLAVE, config.i2cAddress) < 0)
    {
        const int err = errno;
        close(fd_);
        throw std::runtime_error("ssd1306: cannot select I2C address: " + std::string(std::strerror(err)));
    }

    const uint8_t segmentRemap = config.rotate ? 0xA0 : 0xA1;
    const uint8_t scanDirection = config.rotate ? 0xC0 : 0xC8;
    try
    {
        command({0xAE});                 // display off
        command({0xD5, 0x80});           // clock divide
        command({0xA8, 0x3F});           // 64 rows
        command({0xD3, 0x00});           // no display offset
        command({0x40});                 // start line 0
        command({0x8D, 0x14});           // charge pump on
        command({0x20, 0x00});           // horizontal addressing
        command({segmentRemap});
        command({scanDirection});
        command({0xDA, 0x12});           // COM pin layout for 128x64
        command({0x81, static_cast<uint8_t>(config.contrast)});
        command({0xD9, 0xF1});           // pre-charge
        command({0xDB, 0x40});           // VCOMH level
        command({0xA4});                 // show RAM content
        command({0xA6});                 // not inverted
        command({0xAF});                 // display on
    }
    catch (...)
    {
        close(fd_);
        throw;
    }
}

Ssd1306Display::~Ssd1306Display()
{
    if (fd_ < 0) return;
    try { command({0xAE}); } catch (...) {} // do not leave a frozen image
    close(fd_);
}

void Ssd1306Display::command(std::initializer_list<uint8_t> bytes)
{
    uint8_t buffer[8] = {0x00}; // control byte: commands follow
    size_t n = 1;
    for (uint8_t b : bytes) buffer[n++] = b;
    if (write(fd_, buffer, n) != static_cast<ssize_t>(n))
        throw std::runtime_error("ssd1306: I2C write failed: " + std::string(std::strerror(errno)) +
                                 " (check wiring and address)");
}

void Ssd1306Display::sendPage(int page, const uint8_t* data)
{
    command({0x21, 0x00, Framebuffer::Width - 1});                                    // columns
    command({0x22, static_cast<uint8_t>(page), static_cast<uint8_t>(page)});          // this page
    uint8_t buffer[1 + Framebuffer::Width];
    buffer[0] = 0x40; // control byte: data follows
    std::memcpy(buffer + 1, data, Framebuffer::Width);
    if (write(fd_, buffer, sizeof(buffer)) != static_cast<ssize_t>(sizeof(buffer)))
        throw std::runtime_error("ssd1306: I2C write failed: " + std::string(std::strerror(errno)));
}

void Ssd1306Display::update(const ModemStatus& status)
{
    renderStatusScreen(status, fb_);
    try
    {
        // Only send the pages that changed. A full frame takes about 100 ms
        // on a 100 kHz bus; most updates touch one or two pages.
        for (int page = 0; page < Framebuffer::Pages; page++)
        {
            const uint8_t* now = fb_.page(page);
            uint8_t* last = sent_.data() + page * Framebuffer::Width;
            if (!firstFrame_ && std::memcmp(now, last, Framebuffer::Width) == 0) continue;
            sendPage(page, now);
            std::memcpy(last, now, Framebuffer::Width);
        }
        firstFrame_ = false;
        failed_ = false;
    }
    catch (const std::exception& e)
    {
        // A display problem must not stop the modem. Report once, keep trying.
        if (!failed_) FDVE_WARN("%s", e.what());
        failed_ = true;
        firstFrame_ = true; // resend everything when it comes back
    }
}

} // namespace fdve
