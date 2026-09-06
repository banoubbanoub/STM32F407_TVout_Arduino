#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Engine3D.h>

#include "schematic.h"
#include "lenabmp.h"
#include "rectbmp.h"
#include "image_data.h"

// ------------------------------------------------------------
// Pin Definitions & Display Configuration
// ------------------------------------------------------------
#define USER_BUTTON_PIN PA0

#define CE_PIN   PB10
#define CSN_PIN  PB12

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  200

// ------------------------------------------------------------
// Terminal Configuration
// ------------------------------------------------------------
#define TEXT_SCALE     2
#define CHAR_WIDTH     7
#define CHAR_HEIGHT    14

#define TERMINAL_LEFT  30
#define TERMINAL_TOP   25

#define TERMINAL_COLS  ((SCREEN_WIDTH - TERMINAL_LEFT * 2) / CHAR_WIDTH)
#define TERMINAL_ROWS  ((SCREEN_HEIGHT - TERMINAL_TOP - 5) / CHAR_HEIGHT)
#define CURSOR_INTERVAL 500

// ------------------------------------------------------------
// Global Objects & Mode State
// ------------------------------------------------------------
SPIClass SPI_2(PB15, PB14, PB13);  // MOSI, MISO, SCK
RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

Engine3D engine;

uint8_t currentMode = 0;
const uint8_t TOTAL_MODES = 9;
bool lastButtonState = LOW;
int16_t framessostate = 0;

// Terminal State
char terminal[TERMINAL_ROWS][TERMINAL_COLS + 1];
uint16_t cursorX = 0;
uint16_t cursorY = 0;
bool cursorVisible = true;
uint32_t lastCursorTime = 0;
bool rf24Initialized = false;

// ------------------------------------------------------------
// Math & Engine Helpers
// ------------------------------------------------------------
int16_t Height(int x, int y, int l) {
    return engine.cosFixed((x * x + y * y) + l);
}

void SetupMatrix() {
    engine.identity(engine.projectionMatrix);
    engine.identity(engine.modelviewMatrix);
    
    engine.perspective(
        600,    // Field of view parameter
        250,    // Aspect ratio / scaling
        50,     // Near clipping plane
        8192,   // Far clipping plane
        engine.projectionMatrix
    );

    engine.rotateEuler(engine.projectionMatrix, -20, 0, 0);
}

// ------------------------------------------------------------
// Terminal Functions
// ------------------------------------------------------------
void clearTerminalBuffer() {
    for (uint16_t y = 0; y < TERMINAL_ROWS; y++) {
        for (uint16_t x = 0; x < TERMINAL_COLS; x++) {
            terminal[y][x] = ' ';
        }
        terminal[y][TERMINAL_COLS] = '\0';
    }
    cursorX = 0;
    cursorY = 0;
}

void scrollTerminal() {
    for (uint16_t y = 1; y < TERMINAL_ROWS; y++) {
        memcpy(terminal[y - 1], terminal[y], TERMINAL_COLS + 1);
    }

    for (uint16_t x = 0; x < TERMINAL_COLS; x++) {
        terminal[TERMINAL_ROWS - 1][x] = ' ';
    }
    terminal[TERMINAL_ROWS - 1][TERMINAL_COLS] = '\0';

    cursorY = TERMINAL_ROWS - 1;
}

void terminalNewLine() {
    cursorX = 0;
    cursorY++;

    if (cursorY >= TERMINAL_ROWS) {
        scrollTerminal();
    }
}

void terminalPutChar(char c) {
    if (c == '\r' || c == '\n') {
        terminalNewLine();
        return;
    }

    if (c == 127 || c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            terminal[cursorY][cursorX] = ' ';
        }
        return;
    }

    if (c == '\t') {
        uint16_t spaces = 4 - (cursorX & 3);
        while (spaces--) {
            terminalPutChar(' ');
        }
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    if (cursorX >= TERMINAL_COLS) {
        terminalNewLine();
    }

    terminal[cursorY][cursorX] = c;
    cursorX++;

    if (cursorX >= TERMINAL_COLS) {
        terminalNewLine();
    }
}

void initRF24() {
    SPI_2.begin();
    clearTerminalBuffer();

    if (!radio.begin(&SPI_2)) {
        const char *err = "RF24 INIT FAILED!";
        while (*err) terminalPutChar(*err++);
        rf24Initialized = false;
        return;
    }

    radio.openReadingPipe(0, address);
    radio.setPALevel(RF24_PA_LOW);
    radio.startListening();
    rf24Initialized = true;

    terminalPutChar('>');
    terminalPutChar(' ');
    const char *msg = "Ready";
    while (*msg) terminalPutChar(*msg++);
    terminalNewLine();
    terminalPutChar('>');
    terminalPutChar(' ');
}

void DrawTerminalMode() {
    // Check for incoming wireless data
    if (rf24Initialized && radio.available()) {
        char receivedChar;
        radio.read(&receivedChar, sizeof(receivedChar));
        terminalPutChar(receivedChar);
    }

    // Handle cursor blink timer
    uint32_t now = millis();
    if ((now - lastCursorTime) >= CURSOR_INTERVAL) {
        lastCursorTime = now;
        cursorVisible = !cursorVisible;
    }

    // Render Title
    engine.setColor(1);
    engine.setPen(60, 5);
    engine.drawText("WIRELESS TERMINAL", 2);

    // Render Text Rows
    for (uint16_t y = 0; y < TERMINAL_ROWS; y++) {
        engine.setPen(TERMINAL_LEFT, TERMINAL_TOP + y * CHAR_HEIGHT);
        engine.drawText(terminal[y], TEXT_SCALE);
    }

    // Render Cursor
    if (cursorVisible) {
        int cursorPixelX = TERMINAL_LEFT + cursorX * CHAR_WIDTH;
        int cursorPixelY = TERMINAL_TOP + cursorY * CHAR_HEIGHT;

        engine.line(
            cursorPixelX,
            cursorPixelY + CHAR_HEIGHT - 2,
            cursorPixelX + CHAR_WIDTH - 1,
            cursorPixelY + CHAR_HEIGHT - 2,
            1
        );
    }
}

// ------------------------------------------------------------
// Display Modes
// ------------------------------------------------------------
void DrawMesh() {
    SetupMatrix();
    engine.rotateEuler(engine.modelviewMatrix, 0, 0, framessostate);

    engine.setColor(1);
    engine.setPen(80, 10);
    engine.drawText("Dynamic 3D Mesh", 2);

    int o = -framessostate * 2;

    for (int y = -18; y < 18; y++) {
        for (int x = -18; x < 18; x++) {
            int t  = Height(x,     y,     o) * 2 + 2000;
            int nx = Height(x + 1, y,     o) * 2 + 2000;
            int ny = Height(x,     y + 1, o) * 2 + 2000;
            
            int16_t p0[3] = { (int16_t)(x * 120),       (int16_t)(y * 120),       (int16_t)t };
            int16_t p1[3] = { (int16_t)((x + 1) * 120), (int16_t)(y * 120),       (int16_t)nx };
            int16_t p2[3] = { (int16_t)(x * 120),       (int16_t)((y + 1) * 120), (int16_t)ny };

            engine.draw3DSegment(p0, p1);
            engine.draw3DSegment(p0, p2);
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void DrawSphere() {
    SetupMatrix();
    engine.rotateEuler(engine.modelviewMatrix, framessostate, 0, 0);

    engine.setColor(1);
    engine.setPen(80, 10);
    engine.drawText("Matrix-based 3D engine.", 2);

    for (int y = 3; y >= 0; y--) {
        for (int x = 0; x < 40; x++) {
            engine.modelviewMatrix[11] = 1000 + engine.sinFixed((x + y) * 40 + framessostate * 2);
            engine.modelviewMatrix[3]  = 500 * x - 850;
            engine.modelviewMatrix[7]  = 600 * y - 50;
            engine.drawGeoSphere();
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void Draw3DEngine() {
    SetupMatrix();
    engine.rotateEuler(engine.modelviewMatrix, framessostate, 0, 0);

    int sphereset = (framessostate / 120);
    if (sphereset > 2) sphereset = 2;

    for (int y = -sphereset; y <= sphereset; y++) {
        for (int x = -sphereset; x <= sphereset; x++) {
            if (y == 2) continue;
            engine.modelviewMatrix[11] = 1000 + engine.sinFixed((x + y) * 40 + framessostate * 2);
            engine.modelviewMatrix[3]  = 500 * x;
            engine.modelviewMatrix[7]  = 500 * y + 800;
            engine.drawGeoSphere();
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void Lines_on_double_buffered_232x220() {
    engine.setColor(1);
    engine.setPen(40, 10);
    engine.drawText("Lines on double-buffered 232x220.", 2);

    if (framessostate > 60) {
        for (int i = 0; i < 350; i++) {
            engine.setColor((i % 15) + 1);
            engine.line(
                rand() % SCREEN_WIDTH, rand() % (SCREEN_HEIGHT - 30) + 30,
                rand() % SCREEN_WIDTH, rand() % (SCREEN_HEIGHT - 30) + 30,
                (i % 15) + 1
            );
        }
    }

    if (framessostate > 300) framessostate = 0;
}

void Direct_modulation() {
    const char *s = "Direct modulation.\nDMA through the SPI Bus!\nTry it yourself!\n\nSTM32F407 DISC1\n";
    int len = strlen(s);
    if (len > framessostate) len = framessostate;

    char lastct[256];
    strncpy(lastct, s, len);
    lastct[len] = '\0';

    engine.setColor(1);
    engine.setPen(40, 10);
    engine.drawText(lastct, 2);

    if (framessostate > 500) framessostate = 0;
}

void DrawSchematic(int16_t x, int16_t y, const unsigned char *bitmap) {
    engine.setColor(1);
    engine.setPen(60, 10);
    engine.drawText("My schematic:", 2);

    engine.Schematic(x, y, bitmap, 0, 0, 0);

    if (framessostate > 500) framessostate = 0;
    engine.delay(50);
}

void DrawImage(uint16_t line_number, const unsigned char *bitmap) {
    engine.clear();
    engine.bitmap(0, 0, image_6_ntsc, 0, 320, 200);

    if (framessostate > 500) framessostate = 0;
    engine.delay(500);
}

void TVlogo() {
    engine.intro();
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
    // Flash acceleration to avoid AHB stalls
    FLASH->ACR |= FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    pinMode(USER_BUTTON_PIN, INPUT);
    engine.begin();
    engine.setDoubleBuffering(true);
    SetupMatrix();
}

// ------------------------------------------------------------
// Main Loop
// ------------------------------------------------------------
void loop() {
    // Mode Switcher (User Button on PA0)
    bool currentButtonState = digitalRead(USER_BUTTON_PIN);
    if (currentButtonState == HIGH && lastButtonState == LOW) {
        currentMode = (currentMode + 1) % TOTAL_MODES;
        framessostate = 0;

        // Initialize RF24 upon switching into Mode 7
        if (currentMode == 7) {
            initRF24();
        }

        delay(50); // Debounce
    }
    lastButtonState = currentButtonState;

    // Clear Backbuffer
    engine.clear();

    // Mode Dispatcher
    switch (currentMode) {
        case 0:
            DrawMesh();
            break;
        case 1:
            DrawSphere();
            break;
        case 2:
            Draw3DEngine();
            break;
        case 3:
            Lines_on_double_buffered_232x220();
            break;
        case 4:
            Direct_modulation();
            break;
        case 5:
            DrawSchematic(60, 60, schematic);
            break;
        case 6:
            DrawImage(10, image_5_ntsc);
            break;
        case 7:
            DrawTerminalMode();
            break;
        case 8:
            TVlogo();
            break;
        default:
            engine.setColor(1);
            engine.setPen(60, 10);
            engine.drawText("Mode not implemented.", 2);
            break;
    }

    // Display backbuffer frame
    engine.display();

    framessostate++;
}