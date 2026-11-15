# freedv-embedded

A headless FreeDV modem for a Raspberry Pi. It sits between a radio and a
headset and adds RADE V1 to a radio that does not have it.

```
 headset mic ──> [sound card 2] ──> TX: RADE encode ──> [sound card 1] ──> radio audio in
 headphones  <── [sound card 2] <── RX: RADE decode <── [sound card 1] <── radio audio out

 PTT button ──> GPIO in ──> PTT state machine ──> PTT out (radio VOX, or GPIO)
                                  │
                                  └──> display (SNR, AF level, modem state)
```

Status: builds and passes its tests on a Raspberry Pi, not yet tried with real audio hardware. See "What is tested" below.

## Where the FreeDV code comes from

No FreeDV code is copied into this repository. Upstream is split like this
(checked in November 2026):

| Repository | Content | How it gets here |
|---|---|---|
| [freedv/freedv-backend](https://github.com/freedv/freedv-backend) | Audio pipeline, resampling, TX/RX threads, callsign text. Its README says it is meant to be used as a submodule. | git submodule at `external/freedv-backend` |
| [freedv/rade_c](https://github.com/freedv/rade_c) | RADE in C, no Python. V1 and V2 are both in it, selected by a flag in `rade_open()`. | fetched by freedv-backend at build time |
| Opus (FARGAN vocoder), RNNoise | | fetched by rade_c and freedv-backend at build time |

The same backend is used by the upstream headless programs in
[freedv/freedv-integrations](https://github.com/freedv/freedv-integrations)
(FlexRadio and KA9Q). `src/modem/Modem.cpp` follows what those do.

To move to a newer upstream:

```bash
git submodule update --remote external/freedv-backend
```

One thing to know: freedv-backend fetches rade_c from its `main` branch, not
from a pinned commit. A clean build therefore always gets the newest RADE, even
if the submodule commit did not change.

RADE V2: `rade_c` has it behind `RADE_MODE_V2`. The mode name is mapped to
flags in one function, `radeFlagsForMode()` in `src/modem/Modem.cpp`. V2 is
not enabled there yet because freedv-backend's pipeline is only used with V1
upstream today.

## Layout

```
CMakeLists.txt
external/freedv-backend/     submodule, never edited here
config/                      example configuration file
systemd/                     service unit
src/
  main.cpp                   argument parsing, signals
  app/Config.*               TOML file -> Config struct, with validation
  app/Controller.*           builds everything, runs the PTT state machine
  modem/Modem.*              the only code that includes upstream headers
  modem/ModemStatus.h        the snapshot a display receives
  audio/IAudioStream.h       one direction of one device, 16 bit mono
  audio/AlsaStream.*         ALSA backend
  audio/NullStream.*         silence, for running without hardware
  audio/AudioPump.*          one thread per stream, level meter, playback prebuffer
  audio/AudioFactory.*       config -> stream
  ptt/IPtt.h                 IPttInput and IPttOutput
  ptt/GpioLine.*             GPIO through the kernel character device
  ptt/PttFactory.*           config -> PTT input and output
  display/IDisplay.h         update(ModemStatus)
  display/ConsoleDisplay.*   status line on stderr
  display/DisplayFactory.cpp config -> display
tests/
```

Each of `audio`, `ptt` and `display` is an interface plus a factory that picks
an implementation from the `type` key in the config file. Adding hardware means
adding one class and one line in the factory:

| Planned | Where |
|---|---|
| PTT in from a CMX108 or similar | new `IPttInput` in `src/ptt`, new `type` in `PttFactory.cpp` and `Config.cpp` |
| PTT out through hamlib or rigctld | new `IPttOutput` in `src/ptt` |
| OLED (SSD1306 or other) | new `IDisplay` in `src/display` |
| I2S codec, other audio paths | new `IAudioStream` in `src/audio`, or just a different ALSA device name |

Threads at run time: four audio pump threads (one per stream direction), the
backend's TX and RX threads, and the control thread, which polls PTT every 5 ms
and updates the display.

## Build on the Pi

Raspberry Pi OS 64 bit. The build needs network access (Opus, RNNoise and
rade_c are downloaded) and takes a while the first time.

```bash
sudo apt install build-essential cmake git autoconf automake libtool wget patch libasound2-dev libsamplerate0-dev libebur128-dev
```

```bash
git clone --recurse-submodules https://github.com/ManoDaSilva/freedv-embedded.git
```

```bash
cmake -S freedv-embedded -B freedv-embedded/build
```

```bash
cmake --build freedv-embedded/build -j4
```

Use the default Makefile generator. Ninja does not work, because the upstream
build files use `$(MAKE)`.

## Configure and run

Copy `config/freedv-embedded.example.toml` and edit it. Every key is described
there. Device names come from `aplay -L` and `arecord -L`, GPIO chips and lines
from `gpioinfo`.

```bash
./build/src/freedv-embedded -c my.toml --check-config
```

```bash
./build/src/freedv-embedded -c my.toml
```

With `type = "null"` for both sound cards and `type = "none"` for PTT input,
the program runs without any hardware (see `tests/null-audio.toml`).

`systemd/freedv-embedded.service` is a starting point for running it at boot.

## PTT sequence

```
Receive ── PTT pressed ──> Transmit ── PTT released ──> TransmitEnding ── drained ──> Receive
```

- PTT pressed: PTT out is asserted, then `tx_delay_ms` later the modem starts.
- PTT released: microphone audio stops. RADE then sends an end-of-over frame,
  which carries the callsign. The radio has to stay keyed for it.
- When that frame has been handed to the sound card, the program waits
  `tx_tail_ms` and releases PTT out.

With `ptt.output.type = "none"` the radio keys itself from the audio (sound
card VOX), and the sequence only controls the modem.

## What is tested

Run the tests with:

```bash
ctest --test-dir build --output-on-failure
```

| Test | Checks | State |
|---|---|---|
| `config` | Example file parses, defaults, typos and bad values are rejected | passes |
| `audio_pump` | Capture pacing, playback prebuffer and drain, on null devices | passes |
| `modem_loopback` | Speech in, modem signal out, back into the receiver, sync and callsign decode | passes on a Pi |
| `daemon_null_audio` | Whole program on null devices for 3 seconds | passes on a Pi |

Not tested yet, because it needs hardware: ALSA devices, GPIO lines, and CPU
load on a Pi with real audio.

## Next steps

1. Real sound cards: levels, latency, `prebuffer_ms`.
2. GPIO PTT.
3. Measure CPU load in RX and TX on the Pi.
4. OLED driver.
5. PTT out through hamlib or rigctld.
