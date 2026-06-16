# Repository and Demo Notes

## What This Repository Is

This repository is a Waveshare sample project built around the RGB matrix library used to drive LED matrix panels from a Raspberry Pi. It contains the library code, example programs, fonts, hardware drawings, and support files for building and running demos on RGB LED panels.

The most relevant areas are:

- `example/Rasberry-Pi/include/` - public headers for the RGB matrix API.
- `example/Rasberry-Pi/lib/` - the core library implementation.
- `example/Rasberry-Pi/examples-api-use/` - the demo programs and example applications.
- `example/Rasberry-Pi/utils/` - additional tools such as viewers and text scrollers.
- `hardware/` - panel drawings and dimension references.
- `assets/` - images and other documentation assets.

The demo entrypoint is [`example/Rasberry-Pi/examples-api-use/demo-main.cc`](example/Rasberry-Pi/examples-api-use/demo-main.cc), which routes `-D <demo-number>` to a specific demo implementation.

## What I Changed

I changed the default startup settings for the `demo` program so you do not need to type the full hardware setup every time.

The demo now defaults to:

- `--led-rows=64`
- `--led-cols=64`
- `--led-chain=2`
- `--led-parallel=1`
- `--led-no-hardware-pulse`
- `--led-slowdown-gpio=2`

I also updated the usage output so it prints those same defaults when the program shows help.

In practical terms, this means the demo can now be started with a shorter command such as:

```bash
sudo ./demo -D 13
```

You can still override any of the defaults on the command line if a different panel setup is needed.

## How The Demo Program Works

`demo-main.cc` follows a simple pattern:

1. It creates default matrix and runtime option structs.
2. It lets `ParseOptionsFromFlags(...)` override those defaults from the command line.
3. It reads `-D <number>` to choose which demo to run.
4. It creates an `RGBMatrix` with the final options.
5. It instantiates the matching demo class and runs it until interrupted.

Each demo is implemented as a `DemoRunner` subclass or a helper that creates one.

## How To Add Another Demo

To add a new demo, follow the same pattern used by the existing cases in `demo-main.cc`.

1. Add a new class or helper that implements the demo behavior, usually by inheriting from `DemoRunner` and overriding `Run()`.
2. Add the new implementation in `demo-main.cc` or in a separate `.cc` / `.h` pair if the demo is larger.
3. Add a new `case` in the `switch (demo)` statement in `main()`.
4. Assign the next free demo number and return the new demo object from that case.
5. Update the usage text so the new demo number appears in the help output.
6. If the demo needs extra command-line parameters, parse them after `ParseOptionsFromFlags(...)` and document them in the usage text.

A small example of the pattern is already present in the file:

- `case 4` creates the color pulse demo.
- `case 11` creates the brightness pulse demo.
- `case 13` creates the MeteoSwiss weather summary demo and optionally takes a station code.

## Build And Run Notes

The examples are built from `example/Rasberry-Pi/examples-api-use/` with the local `Makefile`.

Typical usage is:

```bash
make
sudo ./demo -D 13
```

If you are testing on macOS, the shared library build may fail on Linux-specific system calls in the lower-level library code. That is unrelated to the demo default change itself.

## Quick Summary

- This repo is a Raspberry Pi RGB matrix demo and library project.
- I changed the demo defaults so the 64x64, 2-chain setup is now the default.
- New demos are added by creating a `DemoRunner` implementation and wiring it into the `switch` in `demo-main.cc`.
