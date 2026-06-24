# DI Plaza Infoscreen

> HSLU MENX FS26 — *DI Plaza Infoscreen (Physical Prototyping)*

A kinetic information installation for the DI Space: a motorized RGB-LED matrix
that autonomously shows **live SBB departures**, **current weather**, and the
**time** as pixel art.

This repository is a fork of Waveshare's
[RGB-Matrix-Px-xx](https://github.com/waveshareteam/RGB-Matrix-Px-xx) (Apache
License 2.0). The hardware-level LED driver and frame-buffering library are used
as-is; the project-specific logic — the **weather**, **train**, and combined
**plaza-info** displays plus their SBB and MeteoSwiss API integrations — is our
own work.

| Role | People |
|------|--------|
| Physical Engineering | Nina Flütsch, Eve Steiger |
| Development | Apisana Senthilrajan, Joel Kammermann |

---

## The three displays

All three live in
[`example/Rasberry-Pi/examples-api-use/`](example/Rasberry-Pi/examples-api-use/)
and render to a **128×64** surface (two daisy-chained 64×64 panels).

### 🌦️ weather-demo

Current weather summary as pixel art, sourced from the **MeteoSwiss** open data
(OGD) forecasting API.

- Source: [`weather-demo.cc`](example/Rasberry-Pi/examples-api-use/weather-demo.cc)
- Runs as demo **13** of the `demo` program; takes an optional station code
  (default `LUZ`).

```bash
sudo ./demo -D 13          # Lucerne (default)
sudo ./demo -D 13 BAS      # Basel
```

### 🚆 train-demo

Live SBB departure board (SBB-styled colors, train graphics), sourced from the
**[transport.opendata.ch](https://transport.opendata.ch)** stationboard API.

- Source: [`train-demo.cc`](example/Rasberry-Pi/examples-api-use/train-demo.cc)
- Runs as demo **14** of the `demo` program; station defaults to `Emmenbrücke`.

```bash
sudo ./demo -D 14
```

### 🏙️ plaza-info

The actual installation: a standalone, fully autonomous infoscreen that combines
**weather + SBB departures + a live clock** on one screen. Weather and train data
are fetched in background threads so the display never blocks, and it refreshes
on its own with no interaction.

- Source: [`plaza-info.cc`](example/Rasberry-Pi/examples-api-use/plaza-info.cc)
- Built as its own binary `plaza-info`. Takes an optional station code
  (default `LUZ`; also `BAS`, `BER`, `ZRH`, `GVE`, `LUG`, or a numeric point id).
  SBB departures are fixed to Emmenbrücke.

```bash
sudo ./plaza-info           # LUZ
sudo ./plaza-info ZRH
```

---

## Build & run

Everything builds from `example/Rasberry-Pi/examples-api-use/` on the Raspberry
Pi:

```bash
cd example/Rasberry-Pi/examples-api-use
make                        # builds the demo and plaza-info binaries
```

Running needs root for GPIO access (privileges are dropped right after the pins
are claimed):

```bash
sudo ./plaza-info LUZ       # the full infoscreen
sudo ./demo -D 13           # just the weather display
sudo ./demo -D 14           # just the train board
```

> Live data requires network access — `curl` must be available on the Pi, since
> both the MeteoSwiss and SBB fetches shell out to it.

> The library uses Linux/Pi-specific GPIO calls; it is meant to be built and run
> on the Raspberry Pi, not on macOS.

### Default panel configuration

The displays target two daisy-chained **64×64** panels:

```
--led-rows=64 --led-cols=64 --led-chain=2 --led-parallel=1 \
--led-no-hardware-pulse --led-slowdown-gpio=2
```

`plaza-info` applies these defaults internally (with `--led-slowdown-gpio=4` for
Pi 4/5). For the `demo` program they are the built-in defaults, so the short
commands above are enough. Any value can be overridden on the command line for a
different panel setup — see
[`example/Rasberry-Pi/wiring.md`](example/Rasberry-Pi/wiring.md) for GPIO wiring
and hardware mappings.

---

## How it fits together

```
Raspberry Pi
├── plaza-info ──┬── MeteoSwiss OGD API   (weather)
│                ├── transport.opendata.ch (SBB departures)
│                └── system clock          (time)
│        ▼
│   RGB matrix library (Waveshare fork, Hub75)  ──►  128×64 LED panels
│
└── (Pi ⇄ Arduino link drives the kinetic / motorized motion)
```

The Pi-side API integrations and the Pi⇄Arduino synchronization logic are
project work; the LED driver underneath is the Waveshare/`rpi-rgb-led-matrix`
library.

---

## Repository layout

```
RGB-Matrix-Px-xx/
├── assets/                       # images
├── DEVELOPER_GUIDE.md            # technical guide to the weather/train demos
├── REPO_AND_DEMO_NOTES.md        # demo defaults and how to add a demo
└── example/Rasberry-Pi/
    ├── include/                  # public LED matrix API headers
    ├── lib/                      # core rgbmatrix library (Waveshare)
    ├── examples-api-use/         # ◄ weather-demo, train-demo, plaza-info live here
    ├── fonts/                    # BDF fonts (4x6, 10x20)
    ├── wiring.md                 # GPIO / Hub75 wiring guide
    └── Makefile
```

---

## Attribution & license

This repository forks **[waveshareteam/RGB-Matrix-Px-xx](https://github.com/waveshareteam/RGB-Matrix-Px-xx)**
(© Waveshare Team), itself built on Henner Zeller's
[`rpi-rgb-led-matrix`](https://github.com/hzeller/rpi-rgb-led-matrix). The LED
driver and frame-buffer code are reused under their original licenses; see
[LICENSE](LICENSE) (Apache 2.0) and individual file headers (the core library is
GPL v2).

Live data is provided by **MeteoSwiss** (open government data) and **SBB /
transport.opendata.ch**.

A special thanks to our mentor **Livia Blättler** for her guidance throughout the
project.
