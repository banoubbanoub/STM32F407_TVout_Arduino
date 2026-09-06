#ifndef __TNTSC_H__
#define __TNTSC_H__

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// Display Specifications
// ============================================================================
#define TNTSC_WIDTH             320                             // Monochrome display width in pixels
#define TNTSC_HEIGHT            200                             // Monochrome display height in pixels
#define TNTSC_BYTES_PER_LINE    (TNTSC_WIDTH / 8)               // 1 bit per pixel = 40 bytes per scanline
#define TNTSC_VRAM_SIZE         (TNTSC_BYTES_PER_LINE * TNTSC_HEIGHT) // Total VRAM size (8,000 bytes)

// ============================================================================
// Hardware Clock Frequencies (STM32 / MCU specifics)
// ============================================================================
#define NTSC_CPU_CLOCK          168000000UL                     // CPU System Clock: 168 MHz
#define NTSC_TIM_CLOCK          84000000UL                      // Timer Clock (APB1 / APB2 Prescaler): 84 MHz
#define NTSC_SPI_CLOCK          21000000UL                      // SPI Data Clock: 21 MHz (Outputs video data bitstream)

// ============================================================================
// NTSC Timing & Line Definitions
// ============================================================================
#define NTSC_LINES              262                             // Total NTSC scanlines per field
#define NTSC_VISIBLE_LINES      200                             // Active visible scanlines on screen

#define NTSC_ARR                5338                            // Timer Auto-Reload Register value (~63.55 us per line)
#define NTSC_HSYNC_TICKS        395                             // Pulse duration for Horizontal Sync (~4.7 us)
#define NTSC_VSYNC_TICKS        4872                            // Pulse duration for Vertical Sync
#define NTSC_VIDEO_START        840                             // Timer offset before triggering SPI/DMA video output

#define NTSC_FIRST_VISIBLE      31                              // First visible scanline index (after VSYNC / back porch)
#define NTSC_LAST_VISIBLE       (NTSC_FIRST_VISIBLE + NTSC_VISIBLE_LINES - 1) // Last visible scanline index (230)

// ============================================================================
// Driver Class Declaration
// ============================================================================
class TNTSC_class
{
public:
    TNTSC_class();

    /**
     * @brief Initializes GPIO, Timers, SPI, and DMA peripherals for video generation.
     * @param spino SPI MOSI pin number to use for video signal output.
     * @param extram Pointer to external SRAM/RAM buffer if allocated outside the class.
     */
    void begin(uint8_t spino = 1, uint8_t* extram = nullptr);

    /**
     * @brief Stops video generation hardware (Timers, DMA, SPI).
     */
    void end();

    /**
     * @brief Returns pointer to the currently active front Video RAM buffer.
     */
    uint8_t* VRAM();

    /**
     * @brief Clears the active Video RAM buffer (fills with 0x00).
     */
    void cls();

    /**
     * @brief Pauses execution for a specified number of vertical frame refreshes (~60Hz).
     * @param frames Number of frames to wait.
     */
    void delay_frame(uint16_t frames);

    /**
     * @brief Registers a hook callback called at the start of the vertical blanking interval.
     */
    void setBktmStartHook(void (*func)());

    /**
     * @brief Registers a hook callback called at the end of the vertical blanking interval.
     */
    void setBktmEndHook(void (*func)());

    // Display Dimension Getters
    uint16_t width() { return TNTSC_WIDTH; }
    uint16_t height() { return TNTSC_HEIGHT; }
    uint16_t vram_size() { return TNTSC_VRAM_SIZE; }

    /**
     * @brief Allocates dynamic memory for double-buffering to prevent visual tearing.
     * @return true if memory allocation succeeded, false otherwise.
     */
    bool initDoubleBuffer();

    /**
     * @brief Swaps the front and back buffers during vertical blanking.
     * @param copy_to_back If true, copies the new front buffer content into the back buffer.
     */
    void swap(bool copy_to_back = false);

    /**
     * @brief Returns pointer to the back buffer when double-buffered, or front buffer otherwise.
     */
    uint8_t* getBackBuffer() { return double_buffered ? vram_back : VRAM(); }

    /**
     * @brief Returns true if double-buffering is currently enabled.
     */
    bool isDoubleBuffered() { return double_buffered; }

    /**
     * @brief Timer 2 Interrupt Handler; handles line counter, sync timing, and DMA triggers.
     */
    void TNTSC_TIM2_Handler();

private:
    uint8_t* vram_front;          // Pointer to active front display buffer
    uint8_t* vram_back;           // Pointer to back draw buffer (for double-buffering)
    bool external_vram;           // Flag indicating if external memory is used for VRAM
    bool double_buffered;         // Flag indicating if double-buffering mode is active

    void (*blank_start_hook)();   // User callback executed at start of V-blank
    void (*blank_end_hook)();     // User callback executed at end of V-blank

    // Hardware peripheral initialization helpers
    void initGPIO();
    void initTIM2();
    void initSPI1();
    void initDMA();
};

extern TNTSC_class TNTSC;         // Global driver instance declaration

#endif // __TNTSC_H__


