# STM32F407 NTSC Video Engine

A lightweight real-time graphics and video engine for the **STM32F407**, designed to generate **320×200 monochrome NTSC composite video** using hardware timers, SPI, DMA, and framebuffer-based rendering.

The project combines low-level STM32 hardware control with a simple graphics/3D engine, making it suitable for experimentation with embedded video, retro-style graphics, and bare-metal/Arduino-based embedded systems.

## Features

* **STM32F407** support
* **320×200 monochrome video**
* NTSC composite video generation
* Hardware timer-based horizontal and vertical synchronization
* SPI-based pixel output
* DMA-compatible video rendering
* Double-buffered framebuffer
* 40-byte framebuffer stride for 320-pixel horizontal resolution
* Fast bitmap rendering
* Low-level framebuffer access
* Lightweight 3D graphics engine integration
* Designed for real-time embedded graphics
* No external video controller required

## Hardware

The project is primarily developed for the:

* STM32F407
* STM32F407 Discovery board
* Composite video output
* SPI1 for video pixel data
* TIM2 for video timing

The design can potentially be adapted to other STM32 devices with suitable timers, SPI peripherals, DMA controllers, and memory.

## Video Resolution

```text
Resolution: 320 × 200
Color:     1-bit monochrome
Framebuffer:
    320 pixels / 8 = 40 bytes per scanline
    40 × 200 = 8000 bytes
```

The framebuffer therefore requires approximately **8 KB** of memory for a single 320×200 monochrome frame.

## Architecture

The video system uses dedicated hardware peripherals to minimize CPU overhead:

```text
                 STM32F407
                     │
          ┌──────────┴──────────┐
          │                     │
        TIM2                  SPI1
          │                     │
     NTSC Sync              Pixel Data
          │                     │
          └──────────┬──────────┘
                     │
                    DMA
                     │
                     ▼
              Composite Video
                     │
                     ▼
                   TV/Monitor
```

The framebuffer stores eight horizontal pixels per byte, allowing the 320-pixel-wide image to be transmitted efficiently.

## Graphics Engine

The repository also contains a lightweight graphics/3D rendering layer built around the NTSC framebuffer.

The graphics system is designed for constrained embedded hardware and focuses on:

* Fast pixel operations
* Bitmap rendering
* Framebuffer manipulation
* Double buffering
* Simple 3D rendering
* Memory-efficient graphics
* Real-time rendering

## Why This Project?

This project is primarily an embedded-systems experiment exploring how far an STM32F407 can be pushed without relying on a dedicated video chip.

It demonstrates how a relatively inexpensive microcontroller can generate a complete composite video signal while simultaneously performing graphics calculations.

The project is also intended as a learning resource for:

* STM32 timers
* SPI
* DMA
* Interrupts
* Framebuffers
* NTSC timing
* Embedded graphics
* Memory optimization
* Real-time rendering

## Status

🚧 **Work in Progress**

The project is actively being developed. Hardware-specific timing, rendering performance, and graphics features may change as the engine evolves.

## Building

The project can be adapted for common STM32 development environments such as:

* PlatformIO
* Arduino framework for STM32
* STM32 development tools

Hardware configuration and pin assignments may need to be adjusted for different STM32F4 boards.

## License

This project is **free and open source**.

It is released under the **MIT License**, allowing you to use, copy, modify, merge, publish, distribute, sublicense, and sell copies of the software, subject to the conditions of the license.

See the [`LICENSE`](LICENSE) file for the complete license text.

## Contributions

Contributions, improvements, optimizations, bug fixes, and experiments are welcome.

If you find a problem or have an idea for improving the video engine, feel free to open an issue or submit a pull request.

## Disclaimer

This project is provided **as-is**, without warranty of any kind. Use it for experimentation, education, and embedded development at your own discretion.

---

**STM32F407 • NTSC • SPI • DMA • Framebuffer • Embedded Graphics • 3D**
