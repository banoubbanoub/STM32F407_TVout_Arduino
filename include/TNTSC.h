#ifndef __TNTSC_H__
#define __TNTSC_H__

#include <Arduino.h>
#include <stdint.h>

#define TNTSC_WIDTH             320
#define TNTSC_HEIGHT            200
#define TNTSC_BYTES_PER_LINE    (TNTSC_WIDTH / 8)
#define TNTSC_VRAM_SIZE         (TNTSC_BYTES_PER_LINE * TNTSC_HEIGHT)

#define NTSC_CPU_CLOCK          168000000UL
#define NTSC_TIM_CLOCK          84000000UL
#define NTSC_SPI_CLOCK          21000000UL

#define NTSC_LINES              262
#define NTSC_VISIBLE_LINES      200

#define NTSC_ARR                5338
#define NTSC_HSYNC_TICKS        395
#define NTSC_VSYNC_TICKS        4872
#define NTSC_VIDEO_START        840

#define NTSC_FIRST_VISIBLE      31
#define NTSC_LAST_VISIBLE       (NTSC_FIRST_VISIBLE + NTSC_VISIBLE_LINES - 1)

class TNTSC_class
{
public:
    TNTSC_class();

    void begin(uint8_t spino = 1, uint8_t* extram = nullptr);
    void end();

    uint8_t* VRAM();
    void cls();
    void delay_frame(uint16_t frames);

    void setBktmStartHook(void (*func)());
    void setBktmEndHook(void (*func)());

    uint16_t width() { return TNTSC_WIDTH; }
    uint16_t height() { return TNTSC_HEIGHT; }
    uint16_t vram_size() { return TNTSC_VRAM_SIZE; }

    bool initDoubleBuffer();
    void swap(bool copy_to_back = false);
    uint8_t* getBackBuffer() { return double_buffered ? vram_back : VRAM(); }
    bool isDoubleBuffered() { return double_buffered; }
    void TNTSC_TIM2_Handler();
private:
    uint8_t* vram_front;
    uint8_t* vram_back;
    bool external_vram;
    bool double_buffered;

    void (*blank_start_hook)();
    void (*blank_end_hook)();

    void initGPIO();
    void initTIM2();
    void initSPI1();
    void initDMA();
  
};

extern TNTSC_class TNTSC;

#endif
