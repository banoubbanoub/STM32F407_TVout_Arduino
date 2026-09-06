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
// Initializes state flags and internal buffer pointers to null.
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
// NTSC_StartDMA: Initiates hardware DMA scanline transmission over SPI1
// Parameters:
//   uint8_t *buffer - Pointer to 40-byte scanline memory array
// =============================================================================
static inline void NTSC_StartDMA(uint8_t *buffer) {
    // 1. Disable DMA Stream prior to configuration (Mandatory per STM32RM)
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN); // Wait for stream to fully disable

    // 2. Clear all previous DMA interrupt flags for Stream 3
    // CFEIF3 = Direct Mode Error, CDMEIF3 = FIFO Error, CTEIF3 = Transfer Error,
    // CHTIF3 = Half Transfer Complete, CTCIF3 = Transfer Complete
    DMA2->LIFCR = DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | 
                  DMA_LIFCR_CTEIF3 | DMA_LIFCR_CHTIF3  | DMA_LIFCR_CTCIF3;

    // 3. Set Memory Source Address and Transfer Data Length (40 bytes = 320 px)
    DMA2_Stream3->M0AR = (uint32_t)buffer;
    DMA2_Stream3->NDTR = TNTSC_BYTES_PER_LINE;

    // 4. Set state active and enable DMA Stream + SPI TX DMA Request
    dma_active = true;
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    SPI1->CR2 |= SPI_CR2_TXDMAEN;
}

// =============================================================================
// INTERRUPT HANDLER: TIM2_IRQHandler (C Linkage)
// Routes standard Cortex-M interrupt vector to C++ class instance method
// =============================================================================
extern "C" void TIM2_IRQHandler(void) {
    TNTSC.TNTSC_TIM2_Handler();
}

// =============================================================================
// TNTSC_TIM2_Handler: Core NTSC Video Engine Interrupt Service Routine
// Executed at 15,742 Hz scanline rate (~63.52 µs per call)
// =============================================================================
void TNTSC_class::TNTSC_TIM2_Handler() {
    uint32_t sr = TIM2->SR;

    // -------------------------------------------------------------------------
    // EVENT 1: TIM2 Update Interrupt (UIF) - Line Start (Sync Pulse Generation)
    // Triggers at the start of every horizontal scanline (Counter Overflow / ARR)
    // -------------------------------------------------------------------------
    if (sr & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF; // Clear Update Interrupt Flag

        // Vertical Sync pulse modulation for Lines 3 to 8 (Broad Equalizing Pulses)
        // VSYNC requires ~58.0 µs active-LOW pulse vs ~4.7 µs for normal HSYNC
        if (ntsc_line >= 3 && ntsc_line <= 8) {
            TIM2->CCR2 = NTSC_VSYNC_TICKS;  // Extended pulse (~58.0 µs)
        } else {
            TIM2->CCR2 = NTSC_HSYNC_TICKS;  // Standard HSYNC pulse (~4.7 µs)
        }

        // Advance line counter and roll over at end of field (262 total lines)
        ntsc_line++;
        if (ntsc_line >= NTSC_LINES) {
            ntsc_line = 0;
        }
    }

    // -------------------------------------------------------------------------
    // EVENT 2: Output Compare 1 Interrupt (CC1IF) - Active Video Start Trigger
    // Triggers after back-porch delay (~12.38 µs post-HSync) to render visible pixels
    // -------------------------------------------------------------------------
    if (sr & TIM_SR_CC1IF) {
        TIM2->SR = ~TIM_SR_CC1IF; // Clear Channel 1 Interrupt Flag

        // Check if current scanline falls inside active vertical display window
        if (ntsc_line >= NTSC_FIRST_VISIBLE && ntsc_line <= NTSC_LAST_VISIBLE) {
            uint16_t y = ntsc_line - NTSC_FIRST_VISIBLE;
            
            // Calculate pointer to scanline in front buffer (40 bytes per row)
            uint8_t *line_ptr = vram_front + (y * TNTSC_BYTES_PER_LINE);
            
            // Trigger 21 Mbps SPI bitstream output via DMA
            NTSC_StartDMA(line_ptr);
        }
    }
}

// =============================================================================
// BEGIN: Initializer for NTSC Engine Subsystems
// Parameters:
//   spino  - SPI interface ID (Defaulted/Configured for SPI1)
//   extram - Optional external VRAM memory address pointer
// =============================================================================
void TNTSC_class::begin(uint8_t spino, uint8_t* extram) {
    // Framebuffer allocation strategy
    if (extram != nullptr) {
        vram_front = extram;      // User-supplied buffer (e.g. SRAM / Static Allocation)
        external_vram = true;
    } else {
        vram_front = (uint8_t*)malloc(TNTSC_VRAM_SIZE); // Dynamic heap allocation (8000 bytes)
        external_vram = false;
    }
    cls();

    // Initialize microcontroller hardware peripherals
    initGPIO();
    initSPI1();
    initDMA();
    initTIM2();
}

// =============================================================================
// END: Graceful Driver Shutdown
// Halts timers, SPI transmission, DMA streams, and releases heap memory
// =============================================================================
void TNTSC_class::end() {
    TIM2->CR1 &= ~TIM_CR1_CEN;        // Disable TIM2 Scanline Timer
    SPI1->CR1 &= ~SPI_CR1_SPE;        // Disable SPI1 Peripheral
    DMA2_Stream3->CR &= ~DMA_SxCR_EN; // Stop DMA Stream

    // Free dynamically allocated VRAM memory buffers
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
// INITDOUBLEBUFFER: Allocates backbuffer for tear-free animation rendering
// Returns: True if memory successfully allocated, false otherwise
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
// SWAP: Atomic Buffer Pointer Exchange (Front vs Back VRAM)
// Parameters:
//   copy_to_back - If true, syncs new backbuffer contents with active front buffer
// =============================================================================
void TNTSC_class::swap(bool copy_to_back) {
    if (!double_buffered) return;

    // Atomically swap memory pointers without ISR collision
    __disable_irq();
    uint8_t* tmp = vram_front;
    vram_front = vram_back;
    vram_back = tmp;
    __enable_irq();

    // Preserve frame contents across swaps if requested
    if (copy_to_back) {
        memcpy(vram_back, vram_front, TNTSC_VRAM_SIZE);
    }
}

// =============================================================================
// INITGPIO: Configures Low-Level GPIO Pin Modes for Video Signal Output
// - PA1: TIM2_CH2 (Alternate Function AF1) -> Composite Sync Pulse Out
// - PA7: SPI1_MOSI (Alternate Function AF5) -> Active Video Pixel Out
// =============================================================================
void TNTSC_class::initGPIO() {
    // Enable GPIOA Peripheral Bus Clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // -------------------------------------------------------------------------
    // PA1 Pin Configuration -> TIM2 Channel 2 (Sync Signals)
    // -------------------------------------------------------------------------
    GPIOA->MODER &= ~(3U << (1 * 2));
    GPIOA->MODER |=  (2U << (1 * 2));         // Alternate Function Mode (10b)
    GPIOA->AFR[0] &= ~(0xF << (1 * 4));
    GPIOA->AFR[0] |=  (1U << (1 * 4));          // Alternate Function AF1 (TIM2)
    GPIOA->OSPEEDR |= (3U << (1 * 2));          // Very High Speed Drive Strength (11b)

    // -------------------------------------------------------------------------
    // PA7 Pin Configuration -> SPI1 MOSI (Video Luminance Signal)
    // -------------------------------------------------------------------------
    GPIOA->MODER &= ~(3U << (7 * 2));
    GPIOA->MODER |=  (2U << (7 * 2));         // Alternate Function Mode (10b)
    GPIOA->AFR[0] &= ~(0xF << (7 * 4));
    GPIOA->AFR[0] |=  (5U << (7 * 4));          // Alternate Function AF5 (SPI1)
    GPIOA->OSPEEDR |= (3U << (7 * 2));          // Very High Speed Drive Strength (11b)
}

// =============================================================================
// INITSPI1: Initializes Master Serial Peripheral Interface for Pixel Bitstreaming
// SPI Clock Speed = PCLK2 (84 MHz) / 4 = 21.0 MHz (~47.6 ns bit width / dot clock)
// =============================================================================
void TNTSC_class::initSPI1() {
    // Enable SPI1 APB2 Bus Clock
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    SPI1->CR1 = 0; // Reset configuration register
    
    // Config: Master Mode, Baud Prescaler = 4, Software NSS management, MSB First
    // BR[2:0] = 001b -> PCLK2 / 4 = 21 MHz Bitrate
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_1 | SPI_CR1_BR_0  |  SPI_CR1_SSI | SPI_CR1_SSM;
  
    SPI1->CR2 = SPI_CR2_TXDMAEN; // Enable DMA transmit requests
    SPI1->CR1 |= SPI_CR1_SPE;    // Enable SPI Peripheral
}

// =============================================================================
// INITDMA: Configures DMA2 Stream 3 Channel 3 for Hardware Memory-to-Peripheral Transfers
// Transfers VRAM line buffer directly into SPI1 Data Register (SPI1->DR)
// =============================================================================
void TNTSC_class::initDMA() {
    // Enable DMA2 AHB Bus Clock
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    // Ensure Stream 3 is disabled before modifying registers
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN);

    // Clear all legacy interrupt flags
    DMA2->LIFCR = DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | 
                  DMA_LIFCR_CTEIF3 | DMA_LIFCR_CHTIF3  | DMA_LIFCR_CTCIF3;

    // Destination Address: SPI1 Data Register
    DMA2_Stream3->PAR = (uint32_t)&(SPI1->DR);
    
    // Control Register Configuration:
    // - CHSEL[2:0] = 011b (Channel 3 -> SPI1_TX)
    // - DIR[1:0]   = 01b  (Memory-to-Peripheral)
    // - MINC       = 1b   (Increment memory source pointer automatically)
    // - PL[1:0]    = 10b  (High DMA Priority)
    // - TCIE       = 1b   (Enable Transfer Complete Interrupt)
    DMA2_Stream3->CR  = (3 << DMA_SxCR_CHSEL_Pos) | 
                        DMA_SxCR_DIR_0             | 
                        DMA_SxCR_MINC              | 
                        DMA_SxCR_PL_1              | 
                        DMA_SxCR_TCIE;              

    // Enable DMA Interrupt Vector in NVIC with highest priority (0)
    NVIC_SetPriority(DMA2_Stream3_IRQn, 0);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

// =============================================================================
// INITTIM2: Configures Scanline Base Timer, Sync PWM Generator & Video Trigger Interrupts
// Core Clock: 84 MHz APB1 Timer Clock | PSC = 0 -> Timer Ticks @ 84 MHz
// ARR = 1334 ticks -> Scanline Duration = 63.523 µs (15,742 Hz scan rate)
// =============================================================================
void TNTSC_class::initTIM2() {
    // Enable TIM2 APB1 Bus Clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->CR1 = 0;
    TIM2->PSC = 0;              // Prescaler = 0 (Run at full 84 MHz timer clock)
    TIM2->ARR = NTSC_ARR;       // Auto-reload value (1334 ticks = 63.523 µs line period)
    TIM2->CCR2 = NTSC_HSYNC_TICKS;  // Channel 2 default pulse width (~4.7 µs HSYNC)
    TIM2->CCR1 = NTSC_VIDEO_START;  // Channel 1 compare match trigger offset (~12.38 µs)

    // -------------------------------------------------------------------------
    // Configure Channel 2 PWM Mode 1 (Inverted Polarity for Active-LOW Sync Signals)
    // -------------------------------------------------------------------------
    TIM2->CCMR1 &= ~(TIM_CCMR1_OC2M_Msk | TIM_CCMR1_OC2PE);
    TIM2->CCMR1 |=  (6 << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE; // PWM Mode 1 + Preload
    TIM2->CCER  |= TIM_CCER_CC2E | TIM_CCER_CC2P;                 // Enable CH2, Active-LOW

    // Enable Update Interrupt (UIE) and Channel 1 Compare Interrupt (CC1IE)
    TIM2->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM2->CR1 |= TIM_CR1_ARPE; // Auto-reload preload enable

    // Generate update event to load registers and clear flags
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;

    // Enable TIM2 Interrupt Vector in NVIC with Priority Level 1
    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    // Start TIM2 Base Counter
    TIM2->CR1 |= TIM_CR1_CEN;
}

// =============================================================================
// INTERRUPT HANDLER: DMA2_Stream3_IRQHandler
// Triggers automatically upon completion of scanline DMA pixel transmission (~15.2 µs)
// =============================================================================
extern "C" void DMA2_Stream3_IRQHandler(void) {
    uint32_t flags = DMA2->LISR;

    // -------------------------------------------------------------------------
    // Transfer Complete Interrupt Flag
    // -------------------------------------------------------------------------
    if (flags & DMA_LISR_TCIF3) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF3;    // Clear Transfer Complete Flag
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;     // Disable SPI DMA Request to stop output
        dma_active = false;
    }
    
    // -------------------------------------------------------------------------
    // Transfer Error Interrupt Flag (Safety Fallback)
    // -------------------------------------------------------------------------
    if (flags & DMA_LISR_TEIF3) {
        DMA2->LIFCR = DMA_LIFCR_CTEIF3;    // Clear Transfer Error Flag
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;     // Disable SPI DMA Request
        dma_active = false;
    }
}

// =============================================================================
// SYSTEMCLOCK_CONFIG: System Clock Configuration for STM32F407 (168 MHz SYSCLK)
// Setup: 8 MHz External Crystal (HSE) -> PLL -> 168 MHz CPU / 84 MHz APB2 / 42 MHz APB1
// =============================================================================
extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Enable Power Control Clock and Set Voltage Scaling for 168 MHz operation
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    // Configure HSE Crystal and PLL Multipliers:
    // VCO In = 8 MHz / M(8) = 1 MHz | VCO Out = 1 MHz * N(336) = 336 MHz
    // SYSCLK = 336 MHz / P(2) = 168 MHz
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        while (1); // Halt on clock init failure
    }

    // Configure Bus Clock Dividers:
    // HCLK = 168 MHz, PCLK1 = 42 MHz (TIM2 = 84 MHz), PCLK2 = 84 MHz (SPI1 = 21 MHz)
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK   | 
                                  RCC_CLOCKTYPE_SYSCLK | 
                                  RCC_CLOCKTYPE_PCLK1  | 
                                  RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider   = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    // Configure Flash Wait States (5 WS for 168 MHz operation)
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        while (1); // Halt on clock config failure
    }
}