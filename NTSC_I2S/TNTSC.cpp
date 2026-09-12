// =============================================================================
// NTSC MONOCHROME SIGNAL SPECIFICATIONS & TIMING SYSTEM ARCHITECTURE
// =============================================================================
//
// 1. NTSC MONOCHROME TIMING SPECIFICATIONS (240p Progressive Standard)
// -----------------------------------------------------------------------------
// Frame Rate            = ~59.94 Hz (Simplified to 60.0 Hz raster timing)
// Total Lines per Frame = 262 Lines (Non-interlaced progressive scan)
// Total Frame Duration  = 262 Lines * 63.555 µs = 16.65 ms per frame
// Horizontal Scan Rate  = 1 / 63.555 µs = 15,734.26 Hz (~15.74 kHz target)
// Full Line Period      = 63.555 µs total duration per scanline
// Active Video Line     = ~52.0 µs visible region available for pixel output
// Sync Level Baseline   = 0.0V (Active LOW pulse)
// Black/Blank Level     = 0.3V (Sync tip to blanking gap offset voltage)
// White Level (Peak)    = 1.0V (Maximum luminance signal voltage level)
//
// Standard Line Pulse Definitions:
// - Horizontal Sync (HSYNC) = ~4.7 µs duration active LOW pulse
// - Vertical Sync (VSYNC)   = ~58.0 µs extended LOW pulse (Lines 2 through 4)
// - Horizontal Back Porch   = ~4.7 µs color burst/blanking delay after HSync
// - Horizontal Front Porch  = ~1.5 µs blanking guard band before HSync
//
// =============================================================================
// 2. MCU CLOCK TREE & MASTER SYSTEM SPEEDS
// -----------------------------------------------------------------------------
// External Crystal (HSE)  = 8.0 MHz
// VCO Input Frequency     = HSE / PLLM = 8 MHz / 8 = 1.0 MHz
// VCO Output Frequency    = VCO Input * PLLN = 1 MHz * 336 = 336.0 MHz
// Core Clock (SYSCLK)     = VCO Freq / PLLP = 336 MHz / 2 = 168.0 MHz
// AHB Core Bus (HCLK)     = SYSCLK / AHB_DIV1 = 168.0 MHz
//
// Peripheral Bus Clocks:
// APB1 Bus Clock (PCLK1)  = HCLK / APB1_DIV4 = 168 MHz / 4 = 42.0 MHz
// TIM2 Peripheral Clock   = PCLK1 * 2 = 42 MHz * 2 = 84.0 MHz 
//   (*Note: STM32 automatically doubles APB timer clocks when APB prescaler > 1)
//
// APB2 Bus Clock (PCLK2)  = HCLK / APB2_DIV2 = 168 MHz / 2 = 84.0 MHz
//   (*Source clock supplying SPI1 master baud rate and DMA2 peripheral port)
//
// =============================================================================
// 3. SCANLINE TIMING & PULSE GENERATION (TIM2 Peripheral)
// -----------------------------------------------------------------------------
// Timer Prescaler         = 4
// Timer Core Tick Freq    = 84 MHz / 4 = 21.0 MHz (21.0 ticks per µs)
// Timer Tick Duration     = 1 / 21 MHz = 47.619 nanoseconds per tick
//
// Auto-Reload Register (ARR) = 1334 ticks
// Computed Line Duration  = 1334 ticks * 47.619 ns = 63.523 µs line period
// Computed Scan Rate      = 21 MHz / 1334 = 15,742.13 Hz 
//   (*Matches NTSC 15,734 Hz standard horizontal line rate with < 0.05% error)
//
// Dynamic Channel Pulse Width Generation (TIM2 CH2 / PA1):
// - Standard H-Sync (CCR2 = 99):   99 ticks * 47.619 ns  = 4.714 µs (Spec: ~4.7 µs)
// - Extended V-Sync (CCR2 = 1218): 1218 ticks * 47.619 ns = 57.999 µs (Spec: ~58 µs)
//
// Video Raster Start Trigger Delay (TIM2 CH1 Interrupt Trigger):
// - Display Offset (CCR1 = 260): 260 ticks * 47.619 ns = 12.38 µs post-HSync delay
//   (*Positions the start of the SPI DMA burst squarely inside the visible raster)
//
// =============================================================================
// 4. SPI PIXEL CLOCK & RASTER RENDERING (SPI1 Output on PA7)
// -----------------------------------------------------------------------------
// Source Clock (APB2 Bus) = 84.0 MHz
// SPI Prescaler Divider   = 4 (SPI_BAUDRATEPRESCALER_4)
// Bitrate / Pixel Clock   = 84 MHz / 4 = 21.0 MHz (21 Mbps serial data burst)
// Pixel Pulse Duration    = 1 / 21 MHz = 47.619 nanoseconds per pixel dot
//
// Line Framebuffer Stride  = 40 Bytes = 320 Serial Bits (Pixels)
// Line Transmission Time  = 320 pixels * 47.619 ns = 15.238 µs active scan window
// Visible Coverage Ratio  = 15.238 µs raster burst / 52.0 µs active line window
//   (*Renders a sharp, centered 320x200 monochrome display box on standard CRT screen)
//
// =============================================================================
// 5. DMA BURST BUFFER & BUS BANDWIDTH (DMA2 Stream 3)
// -----------------------------------------------------------------------------
// Framebuffer Memory Alloc = 320 px * 200 lines / 8 bits = 8000 Bytes (8 KB VRAM)
// DMA Source Target        = SRAM Framebuffer Address Pointer (Incrementing)
// DMA Destination Target   = SPI1 Data Register (SPI1->DR, Fixed Address)
// DMA Transfer Size        = 40 Bytes per DMA enable trigger burst (1 scanline)
// DMA Streaming Bandwidth  = 40 Bytes / 15.238 µs = 2.625 MB/sec burst rate
// Core CPU AHB Utilization = ~0.024% total bus bandwidth overhead per line
// Interrupt Execution      = Triggers DMA1_CH3 handle ISR upon line transfer completion
// =============================================================================


// =============================================================================
// FILE: TNTSC.cpp
// SYSTEM: STM32F407 NTSC Video Generation Driver (Monochrome 240p Progressive)
// ARCHITECTURE: TIM2 Base Scanline Timer + SPI1 MOSI Bitstreaming + DMA2 Stream 3
// =============================================================================
// 
// OVERVIEW:
// This driver implements a hardware-accelerated monochrome NTSC video signal
// generator on an STM32F4 microcontroller running at 168 MHz. 
//
// TIMING & SIGNAL GENERATION:
// - TIM2 Channel 2 (PA1) outputs the combined HSYNC/VSYNC active-LOW pulse via PWM.
// - TIM2 Channel 1 triggers an interrupt at the back-porch offset to start video DMA.
// - DMA2 Stream 3 transfers frame data (320x200 monochrome, 40 bytes/line) to SPI1.
// - SPI1 MOSI (PA7) shifts out video bits at 21 Mbps (~47.6 ns/pixel dot clock).
//
// SIGNAL VOLTAGE LEVELS (External Resistor DAC Required):
// - PA1 (Sync) + PA7 (Video) combined via resistor network into 75-ohm load:
//   - HSYNC/VSYNC Active Low  = 0.0V (Sync Tip)
//   - Blanking / Black Level  = 0.3V (Sync Inactive, SPI MOSI Low)
//   - White Level (Peak Luminance) = 1.0V (Sync Inactive, SPI MOSI High)
// =============================================================================

#include "TNTSC.h"
#include "stm32f407xx.h"
#include "stm32f4xx_hal.h"
#include <cstring>
#include <cstdlib>

// -----------------------------------------------------------------------------
// PRIVATE VOLATILE STATE VARIABLES
// -----------------------------------------------------------------------------
// Current scanline tracker (0 to 261 for progressive NTSC framing)
static volatile uint16_t ntsc_line = 0;

// Flag indicating if a DMA transfer is currently in progress for the active scanline
static volatile bool dma_active = false;

// Instantiate the global driver class object
TNTSC_class TNTSC;

// =============================================================================
// CLASS CONSTRUCTOR
// =============================================================================
TNTSC_class::TNTSC_class()
    : vram_front(nullptr), vram_back(nullptr), 
      external_vram(false), double_buffered(false),
      blank_start_hook(nullptr), blank_end_hook(nullptr) {}

// =============================================================================
// VRAM: Returns pointer to active front framebuffer
// =============================================================================
uint8_t* TNTSC_class::VRAM() {
    return vram_front;
}

// =============================================================================
// CLS: Clears front VRAM framebuffer to solid black (0x00)
// =============================================================================
void TNTSC_class::cls() {
    if (vram_front) {
        memset(vram_front, 0, TNTSC_VRAM_SIZE);
    }
}

// =============================================================================
// NTSC_StartDMA: Initiates hardware DMA scanline transmission over I2S2
// =============================================================================
static inline void NTSC_StartDMA(uint8_t *buffer) {
    // 1. Disable DMA1 Stream 4 prior to configuration
    DMA1_Stream4->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream4->CR & DMA_SxCR_EN); // Wait for stream to fully disable

    // 2. Clear all previous DMA interrupt flags for DMA1 Stream 4
    DMA1->HIFCR = DMA_HIFCR_CFEIF4 | DMA_HIFCR_CDMEIF4 |
                  DMA_HIFCR_CTEIF4 | DMA_HIFCR_CHTIF4  | DMA_HIFCR_CTCIF4;

    // 3. Set Memory Source Address and Transfer Length (16-bit half-words)
    DMA1_Stream4->M0AR = (uint32_t)buffer;
    DMA1_Stream4->NDTR = TNTSC_BYTES_PER_LINE / 2; // 20 half-words for 40 bytes

    // 4. Enable DMA Stream, then enable I2S2 TX DMA Request
    dma_active = true;
    DMA1_Stream4->CR |= DMA_SxCR_EN;
    SPI2->CR2 |= SPI_CR2_TXDMAEN; // Enable DMA request on I2S2/SPI2
}

// =============================================================================
// INTERRUPT HANDLER: TIM2_IRQHandler (C Linkage)
// =============================================================================
extern "C" void TIM2_IRQHandler(void) {
    TNTSC.TNTSC_TIM2_Handler();
}


static inline uint8_t reverse_bits8(uint8_t v) {
    v = ((v & 0xF0) >> 4) | ((v & 0x0F) << 4);
    v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
    v = ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
    return v;
}


static uint8_t dma_line_buf[TNTSC_BYTES_PER_LINE];

static inline void NTSC_SwapLineBytes(const uint8_t *src, uint8_t *dst) {
    for (uint16_t i = 0; i < TNTSC_BYTES_PER_LINE; i += 2) {
        dst[i] = src[i + 1];
        dst[i + 1] = src[i];
    }
}
// =============================================================================
// TNTSC_TIM2_Handler: Core NTSC Video Engine Interrupt Service Routine
// =============================================================================
void TNTSC_class::TNTSC_TIM2_Handler() {
    uint32_t sr = TIM2->SR;

    // EVENT 1: TIM2 Update Interrupt (UIF) - Line Start (Sync Pulse Generation)
    if (sr & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF; // Clear Update Interrupt Flag

        if (ntsc_line >= 3 && ntsc_line <= 8) {
            TIM2->CCR2 = NTSC_VSYNC_TICKS;  // Extended pulse (~58.0 µs)
        } else {
            TIM2->CCR2 = NTSC_HSYNC_TICKS;  // Standard HSYNC pulse (~4.7 µs)
        }

        ntsc_line++;
        if (ntsc_line >= NTSC_LINES) {
            ntsc_line = 0;
        }
    }

    // EVENT 2: Output Compare 1 Interrupt (CC1IF) - Active Video Start Trigger
    if (sr & TIM_SR_CC1IF) {
        TIM2->SR = ~TIM_SR_CC1IF; // Clear Channel 1 Interrupt Flag

        if (ntsc_line >= NTSC_FIRST_VISIBLE && ntsc_line <= NTSC_LAST_VISIBLE) {
            uint16_t y = ntsc_line - NTSC_FIRST_VISIBLE;
           const  uint8_t *line_ptr = vram_front + (y * TNTSC_BYTES_PER_LINE);
            
         
          NTSC_SwapLineBytes(line_ptr, dma_line_buf);
         NTSC_StartDMA(dma_line_buf);
          
        }
    }
}

// =============================================================================
// BEGIN: Initializer for NTSC Engine Subsystems
// =============================================================================
void TNTSC_class::begin(uint8_t spino, uint8_t* extram) {
    if (extram != nullptr) {
        vram_front = extram;
        external_vram = true;
    } else {
        vram_front = (uint8_t*)malloc(TNTSC_VRAM_SIZE);
        external_vram = false;
    }
    cls();

    initGPIO();
    initI2S3(); // Named initI2S3 per class signature, configures I2S2 hardware
    initDMA();
    initTIM2();
}

// =============================================================================
// END: Driver Shutdown
// =============================================================================
void TNTSC_class::end() {
    TIM2->CR1 &= ~TIM_CR1_CEN;          // Disable TIM2
    SPI2->I2SCFGR &= ~SPI_I2SCFGR_I2SE; // Disable I2S2
    DMA1_Stream4->CR &= ~DMA_SxCR_EN;   // Disable DMA

    if (!external_vram && vram_front) {
        free(vram_front);
        vram_front = nullptr;
    }
    if (vram_back) {
        free(vram_back);
        vram_back = nullptr;
    }
}

// =============================================================================
// INITDOUBLEBUFFER
// =============================================================================
bool TNTSC_class::initDoubleBuffer() {
    if (!vram_back) {
        vram_back = (uint8_t*)malloc(TNTSC_VRAM_SIZE);
        if (vram_back) {
            memset(vram_back, 0, TNTSC_VRAM_SIZE);
            double_buffered = true;
            return true;
        }
    }
    return double_buffered;
}

// =============================================================================
// SWAP
// =============================================================================
void TNTSC_class::swap(bool copy_to_back) {
    if (!double_buffered) return;

    __disable_irq();
    uint8_t* tmp = vram_front;
    vram_front = vram_back;
    vram_back = tmp;
    __enable_irq();

    if (copy_to_back) {
        memcpy(vram_back, vram_front, TNTSC_VRAM_SIZE);
    }
}

// =============================================================================
// INITGPIO: Configures Low-Level Pins for Video Signal Output
// - PA1: TIM2_CH2 (AF1)  -> Composite Sync Pulse Out
// - PB13: I2S2_CK (AF5)  -> Pixel Clock Output (Optional monitoring)
// - PB15: I2S2_SD (AF5)  -> Active Video Pixel Stream
// =============================================================================
void TNTSC_class::initGPIO() {
    // Enable GPIOA and GPIOB Peripheral Clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    // PA1 Pin Configuration -> TIM2 Channel 2 (Sync Out)
    GPIOA->MODER &= ~(3U << (1 * 2));
    GPIOA->MODER |=  (2U << (1 * 2));         // Alternate Function Mode
    GPIOA->AFR[0] &= ~(0xF << (1 * 4));
    GPIOA->AFR[0] |=  (1U << (1 * 4));         // AF1 (TIM2)
    GPIOA->OSPEEDR |= (3U << (1 * 2));         // Very High Speed

    // PB15 Pin Configuration -> I2S2_SD Serial Data Line (Video Out)
    GPIOB->MODER &= ~(3U << (15 * 2));
    GPIOB->MODER |=  (2U << (15 * 2));         // Alternate Function Mode
    GPIOB->AFR[1] &= ~(0xFU << ((15 - 8) * 4));
    GPIOB->AFR[1] |=  (5U << ((15 - 8) * 4));  // AF5 (SPI2/I2S2)
    GPIOB->OSPEEDR |= (3U << (15 * 2));        // Very High Speed
}

// =============================================================================
// INITI2S3: Configures I2S2 Hardware (Mapped to SPI2 Peripheral)
// =============================================================================
void TNTSC_class::initI2S3() {
    // 1. Enable SPI2 / I2S2 clock
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    volatile uint32_t tmp = RCC->APB1ENR;
    (void)tmp;

    // 2. Disable I2S before modifying configuration
    SPI2->I2SCFGR &= ~SPI_I2SCFGR_I2SE;

    // 3. Configure PLLI2S Clock Generation
    RCC->CR &= ~RCC_CR_PLLI2SON;
    while (RCC->CR & RCC_CR_PLLI2SRDY);

    // Generate I2SCLK = 96 MHz (1 MHz ref * 192 N / 2 R)
    RCC->PLLI2SCFGR = (192U << RCC_PLLI2SCFGR_PLLI2SN_Pos) |
                      (2U   << RCC_PLLI2SCFGR_PLLI2SR_Pos);

    RCC->CR |= RCC_CR_PLLI2SON;
    while (!(RCC->CR & RCC_CR_PLLI2SRDY));

    // 4. Set Bit Clock Divider (BCLK = 96 MHz / (2 * 8) = 6 MHz)
    SPI2->I2SPR = (8u << SPI_I2SPR_I2SDIV_Pos);//8u

    // 5. Configure I2S Mode: Master TX, MSB-Justified Data Frame
    SPI2->I2SCFGR = SPI_I2SCFGR_I2SMOD
                  | SPI_I2SCFGR_I2SCFG_1    // Master Transmitter
                  | SPI_I2SCFGR_I2SSTD_0;   // MSB Justified

    // 6. Enable DMA transmit requests & Enable Peripheral
    SPI2->CR2 |= SPI_CR2_TXDMAEN;
    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SE;
}

// =============================================================================
// INITDMA: Configures DMA1 Stream 4 Channel 0 (Servicing SPI2_TX / I2S2_TX)
// =============================================================================
void TNTSC_class::initDMA() {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;

    // Disable Stream 4 for configuration
    DMA1_Stream4->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream4->CR & DMA_SxCR_EN);

    // Clear interrupt status flags for Stream 4
    DMA1->HIFCR = DMA_HIFCR_CFEIF4 | DMA_HIFCR_CDMEIF4 |
                  DMA_HIFCR_CTEIF4 | DMA_HIFCR_CHTIF4  | DMA_HIFCR_CTCIF4;

    // Set Peripheral Destination Address to SPI2 Data Register
    DMA1_Stream4->PAR = (uint32_t)&(SPI2->DR);

    // Configure Stream 4: Channel 0, Mem-to-Periph, 16-bit MSIZE/PSIZE, High Priority
    DMA1_Stream4->CR = (0U << DMA_SxCR_CHSEL_Pos)  |
                       DMA_SxCR_DIR_0              | // Memory-to-peripheral
                       DMA_SxCR_MINC               | // Memory increment
                       DMA_SxCR_MSIZE_0            | // 16-bit memory data size
                       DMA_SxCR_PSIZE_0            | // 16-bit peripheral data size
                       DMA_SxCR_PL_1               | // High priority
                       DMA_SxCR_TCIE;                // Transfer complete IRQ

    // Enable DMA1 Stream 4 Interrupt in NVIC
    NVIC_SetPriority(DMA1_Stream4_IRQn, 0);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

// =============================================================================
// INITTIM2: Configures Scanline Base Timer & Sync Signals
// =============================================================================
void TNTSC_class::initTIM2() {
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->CR1 = 0;
    TIM2->PSC = 0;
    TIM2->ARR = NTSC_ARR - 1;
    TIM2->CCR2 = NTSC_HSYNC_TICKS;
    TIM2->CCR1 = NTSC_VIDEO_START;

    TIM2->CCMR1 &=~(TIM_CCMR1_OC2M_Msk | TIM_CCMR1_OC2PE); //~(TIM_CCMR1_OC2M_Msk | TIM_CCMR1_CC2S_Msk);
    TIM2->CCMR1 |= (0x6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM2->CCER  |= TIM_CCER_CC2E | TIM_CCER_CC2P; // Active-LOW CH2 Output

    TIM2->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM2->CR1 |= TIM_CR1_ARPE;

    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;

    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1 |= TIM_CR1_CEN;
}

// =============================================================================
// INTERRUPT HANDLER: DMA1_Stream4_IRQHandler
// =============================================================================
extern "C" void DMA1_Stream4_IRQHandler(void) {
    uint32_t flags = DMA1->HISR;

    if (flags & (DMA_HISR_TCIF4 | DMA_HISR_TEIF4)) {
        DMA1->HIFCR = DMA_HIFCR_CTCIF4 | DMA_HIFCR_CTEIF4;

        // Wait until I2S finishes transmitting the last frame
        while (SPI2->SR & SPI_SR_BSY);
        
        // Disable DMA request to avoid underrun loops
        SPI2->CR2 &= ~SPI_CR2_TXDMAEN;
        
        // Force I2S line to BLACK (0x0000) during blanking intervals
        SPI2->DR = 0x0000;
        
        dma_active = false;
    }
}

// =============================================================================
// SYSTEMCLOCK_CONFIG: System Clock Configuration for STM32F407 (168 MHz)
// =============================================================================
extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        while (1);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK   | 
                                  RCC_CLOCKTYPE_SYSCLK | 
                                  RCC_CLOCKTYPE_PCLK1  | 
                                  RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider   = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        while (1);
    }
}