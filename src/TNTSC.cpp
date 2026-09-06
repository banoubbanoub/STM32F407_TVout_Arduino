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


#include "TNTSC.h"
#include "stm32f407xx.h"
#include "stm32f4xx_hal.h"

static volatile uint16_t ntsc_line = 0;
static volatile bool dma_active = false;

TNTSC_class::TNTSC_class()
    : vram_front(nullptr), vram_back(nullptr), 
      external_vram(false), double_buffered(false),
      blank_start_hook(nullptr), blank_end_hook(nullptr) {}

uint8_t* TNTSC_class::VRAM() {
    return vram_front;
}

void TNTSC_class::cls() {
    if (vram_front) {
        memset(vram_front, 0, TNTSC_VRAM_SIZE);
    }
}

static inline void NTSC_StartDMA(uint8_t *buffer) {
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN);

    DMA2->LIFCR = DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | 
                  DMA_LIFCR_CTEIF3 | DMA_LIFCR_CHTIF3  | DMA_LIFCR_CTCIF3;

    DMA2_Stream3->M0AR = (uint32_t)buffer;
    DMA2_Stream3->NDTR = TNTSC_BYTES_PER_LINE;

    dma_active = true;
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    SPI1->CR2 |= SPI_CR2_TXDMAEN;
}

// ------------------------------------------------------------------
// Hardware Interrupt Vector Linkage
// ------------------------------------------------------------------
extern "C" void TIM2_IRQHandler(void) {
    TNTSC.TNTSC_TIM2_Handler();
}

void TNTSC_class::TNTSC_TIM2_Handler() {
    uint32_t sr = TIM2->SR;

    // Handle Timer Update Event (Sync pulse Generation)
    if (sr & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;

        if (ntsc_line >= 3 && ntsc_line <= 8) {
            TIM2->CCR2 = NTSC_VSYNC_TICKS;
        } else {
            TIM2->CCR2 = NTSC_HSYNC_TICKS;
        }

        ntsc_line++;
        if (ntsc_line >= NTSC_LINES) {
            ntsc_line = 0;
        }
    }

    // Handle Output Compare 1 Event (Video DMA Start)
    if (sr & TIM_SR_CC1IF) {
        TIM2->SR = ~TIM_SR_CC1IF;

        if (ntsc_line >= NTSC_FIRST_VISIBLE && ntsc_line <= NTSC_LAST_VISIBLE) {
            uint16_t y = ntsc_line - NTSC_FIRST_VISIBLE;
            // Fetch directly from vram_front to handle active frame transfers cleanly
            uint8_t *line_ptr = vram_front + (y * TNTSC_BYTES_PER_LINE);
            NTSC_StartDMA(line_ptr);
        }
    }
}

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
    initSPI1();
    initDMA();
    initTIM2();
   
}

void TNTSC_class::end() {
    TIM2->CR1 &= ~TIM_CR1_CEN;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;

    if (!external_vram && vram_front) {
        free(vram_front);
        vram_front = nullptr;
    }
    if (vram_back) {
        free(vram_back);
        vram_back = nullptr;
    }
}

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

void TNTSC_class::swap(bool copy_to_back) {
    if (!double_buffered) return;

    // Atomically swap buffer pointers
    __disable_irq();
    uint8_t* tmp = vram_front;
    vram_front = vram_back;
    vram_back = tmp;
    __enable_irq();

    if (copy_to_back) {
        memcpy(vram_back, vram_front, TNTSC_VRAM_SIZE);
    }
}

void TNTSC_class::initGPIO() {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // PA1 -> TIM2_CH2 (Alternate Function AF1)
    GPIOA->MODER &= ~(3U << (1 * 2));
    GPIOA->MODER |=  (2U << (1 * 2));
    GPIOA->AFR[0] &= ~(0xF << (1 * 4));
    GPIOA->AFR[0] |=  (1U << (1 * 4));
    GPIOA->OSPEEDR |= (3U << (1 * 2));

    // PA7 -> SPI1_MOSI (Alternate Function AF5)
    GPIOA->MODER &= ~(3U << (7 * 2));
    GPIOA->MODER |=  (2U << (7 * 2));
    GPIOA->AFR[0] &= ~(0xF << (7 * 4));
    GPIOA->AFR[0] |=  (5U << (7 * 4));
    GPIOA->OSPEEDR |= (3U << (7 * 2));
}

void TNTSC_class::initSPI1() {
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    SPI1->CR1 = 0;
    // Master mode, Baud rate = PCLK2 / 4 (84 MHz / 4 = 21 MHz), Software NSS, MSB first
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_1 | SPI_CR1_BR_0  |  SPI_CR1_SSI | SPI_CR1_SSM;
  
    SPI1->CR2 = SPI_CR2_TXDMAEN;
    SPI1->CR1 |= SPI_CR1_SPE;
}

void TNTSC_class::initDMA() {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN);

    DMA2->LIFCR = DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | 
                  DMA_LIFCR_CTEIF3 | DMA_LIFCR_CHTIF3  | DMA_LIFCR_CTCIF3;

    DMA2_Stream3->PAR = (uint32_t)&(SPI1->DR);
    DMA2_Stream3->CR  = (3 << DMA_SxCR_CHSEL_Pos) | // Channel 3
                        DMA_SxCR_DIR_0             | // Memory-to-peripheral
                        DMA_SxCR_MINC              | // Memory increment
                        DMA_SxCR_PL_1              | // High priority
                        DMA_SxCR_TCIE;              // Transfer complete interrupt

    NVIC_SetPriority(DMA2_Stream3_IRQn, 0);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

void TNTSC_class::initTIM2() {
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->CR1 = 0;
    TIM2->PSC = 0;
    TIM2->ARR = NTSC_ARR;
    TIM2->CCR2 = NTSC_HSYNC_TICKS;
    TIM2->CCR1 = NTSC_VIDEO_START;

    // TIM2 CH2 PWM Mode 1 with inverted output polarity for active-LOW sync
    TIM2->CCMR1 &= ~(TIM_CCMR1_OC2M_Msk | TIM_CCMR1_OC2PE);
    TIM2->CCMR1 |=  (6 << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM2->CCER  |= TIM_CCER_CC2E | TIM_CCER_CC2P;

    TIM2->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM2->CR1 |= TIM_CR1_ARPE;

    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;

    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1 |= TIM_CR1_CEN;
}

extern "C" void DMA2_Stream3_IRQHandler(void) {
    uint32_t flags = DMA2->LISR;

    if (flags & DMA_LISR_TCIF3) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF3;
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
        dma_active = false;
    }
    if (flags & DMA_LISR_TEIF3) {
        DMA2->LIFCR = DMA_LIFCR_CTEIF3;
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
        dma_active = false;
    }
}

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
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        while (1);
    }
}

TNTSC_class TNTSC;