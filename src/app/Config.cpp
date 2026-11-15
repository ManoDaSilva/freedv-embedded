#include "app/Config.h"

#include <initializer_list>
#include <sstream>
#include <stdexcept>

#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

namespace fdve {

namespace {

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error("config: " + message);
}

// Returns the sub-table at "name", or an empty table. Rejects unknown keys.
toml::table section(const toml::table& parent, const std::string& path, const char* name,
                    std::initializer_list<std::string_view> allowedKeys)
{
    const std::string fullPath = path.empty() ? name : path + "." + name;
    toml::table result;
    if (auto node = parent.get(name))
    {
        auto table = node->as_table();
        if (table == nullptr) fail("'" + fullPath + "' must be a table");
        result = *table;
    }
    for (auto&& [key, value] : result)
    {
        bool known = false;
        for (auto allowed : allowedKeys) known = known || key.str() == allowed;
        if (!known) fail("unknown key '" + fullPath + "." + std::string(key.str()) + "'");
    }
    return result;
}

template <typename T>
void read(const toml::table& table, const std::string& path, const char* key, T& out)
{
    auto node = table.get(key);
    if (node == nullptr) return;
    auto value = node->value_exact<T>();
    if (!value) fail("'" + path + "." + key + "' has the wrong type");
    out = *value;
}

void readInt(const toml::table& table, const std::string& path, const char* key, int& out, int min, int max)
{
    int64_t value = out;
    read(table, path, key, value);
    if (value < min || value > max)
        fail("'" + path + "." + key + "' must be between " + std::to_string(min) + " and " + std::to_string(max));
    out = static_cast<int>(value);
}

void readSoundCard(const toml::table& audio, const char* name, SoundCardConfig& out)
{
    const std::string path = std::string("audio.") + name;
    auto t = section(audio, "audio", name, {"type", "capture_device", "playback_device", "sample_rate"});
    read(t, path, "type", out.type);
    read(t, path, "capture_device", out.captureDevice);
    read(t, path, "playback_device", out.playbackDevice);
    readInt(t, path, "sample_rate", out.sampleRate, 8000, 192000);
    if (out.type != "alsa" && out.type != "null") fail("'" + path + ".type' must be \"alsa\" or \"null\"");
}

void readGpio(const toml::table& t, const std::string& path, GpioLine::Config& out, bool isInput)
{
    read(t, path, "chip", out.chip);
    readInt(t, path, "line", out.line, -1, 1023);
    read(t, path, "active_low", out.activeLow);
    if (!isInput) return;
    std::string bias = "none";
    read(t, path, "bias", bias);
    if (bias == "none") out.bias = GpioLine::Bias::None;
    else if (bias == "pull-up") out.bias = GpioLine::Bias::PullUp;
    else if (bias == "pull-down") out.bias = GpioLine::Bias::PullDown;
    else fail("'" + path + ".bias' must be \"none\", \"pull-up\" or \"pull-down\"");
    readInt(t, path, "debounce_ms", out.debounceMs, 0, 1000);
}

} // namespace

Config parseConfig(std::string_view text)
{
    toml::table root;
    try
    {
        root = toml::parse(text);
    }
    catch (const toml::parse_error& e)
    {
        std::ostringstream message;
        message << e.description() << " (line " << e.source().begin.line << ")";
        fail(message.str());
    }

    Config config;
    for (auto&& [key, value] : root)
    {
        auto k = key.str();
        if (k != "modem" && k != "audio" && k != "ptt" && k != "display")
            fail("unknown section '" + std::string(k) + "'");
    }

    auto modem = section(root, "", "modem", {"mode", "callsign"});
    read(modem, "modem", "mode", config.mode);
    read(modem, "modem", "callsign", config.callsign);
    if (config.mode != "RADEV1") fail("'modem.mode' must be \"RADEV1\"");
    if (config.callsign.size() > 8) fail("'modem.callsign' is limited to 8 characters");

    auto audio = section(root, "", "audio", {"period_ms", "prebuffer_ms", "radio", "headset"});
    readInt(audio, "audio", "period_ms", config.periodMs, 5, 100);
    readInt(audio, "audio", "prebuffer_ms", config.prebufferMs, 0, 500);
    readSoundCard(audio, "radio", config.radio);
    readSoundCard(audio, "headset", config.headset);

    auto ptt = section(root, "", "ptt", {"input", "output"});
    auto in = section(ptt, "ptt", "input", {"type", "chip", "line", "active_low", "bias", "debounce_ms"});
    read(in, "ptt.input", "type", config.pttInput.type);
    if (config.pttInput.type != "gpio" && config.pttInput.type != "none")
        fail("'ptt.input.type' must be \"gpio\" or \"none\"");
    readGpio(in, "ptt.input", config.pttInput.gpio, true);
    if (config.pttInput.type == "gpio" && config.pttInput.gpio.line < 0) fail("'ptt.input.line' is required");

    auto out = section(ptt, "ptt", "output", {"type", "chip", "line", "active_low", "tx_delay_ms", "tx_tail_ms"});
    read(out, "ptt.output", "type", config.pttOutput.type);
    if (config.pttOutput.type != "gpio" && config.pttOutput.type != "none")
        fail("'ptt.output.type' must be \"none\" or \"gpio\"");
    readGpio(out, "ptt.output", config.pttOutput.gpio, false);
    if (config.pttOutput.type == "gpio" && config.pttOutput.gpio.line < 0) fail("'ptt.output.line' is required");
    readInt(out, "ptt.output", "tx_delay_ms", config.txDelayMs, 0, 2000);
    readInt(out, "ptt.output", "tx_tail_ms", config.txTailMs, 0, 2000);

    auto display = section(root, "", "display", {"type", "refresh_ms"});
    read(display, "display", "type", config.display.type);
    if (config.display.type != "console" && config.display.type != "none")
        fail("'display.type' must be \"console\" or \"none\"");
    readInt(display, "display", "refresh_ms", config.display.refreshMs, 50, 10000);

    return config;
}

Config loadConfig(const std::string& path)
{
    toml::table root;
    std::ostringstream text;
    try
    {
        text << toml::parse_file(path);
    }
    catch (const toml::parse_error& e)
    {
        std::ostringstream message;
        message << path << ": " << e.description() << " (line " << e.source().begin.line << ")";
        fail(message.str());
    }
    return parseConfig(text.str());
}

} // namespace fdve
