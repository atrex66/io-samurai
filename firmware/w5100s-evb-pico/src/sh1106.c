#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "sh1106.h"

static const uint8_t init_sequence[] = {
    0xAE,       // Kijelző kikapcsolása
    0x20, 0x00, // Vízszintes címzési mód
    0xA8, 0x3F, // Multiplex arány: 64
    0xD3, 0x00, // Eltolás: 0
    0x40,       // Kezdő sor: 0
    0xA1,       // Szegmens újraleképezés
    0xC8,       // COM szkennelés: fordított
    0xDA, 0x12, // COM pin konfiguráció
    0x81, 0x7F, // Kontraszt (közepes érték, próbáld 0xCF vagy 0xFF is)
    0xA4,       // Normál tartalom
    0xA6,       // Normál kijelzés (nem invertált)
    0xD5, 0x80, // Órajel beállítása
    0x8D, 0x14, // Töltéspumpa engedélyezése
    0xAF        // Kijelző bekapcsolása (utolsónak!)
};

// Puffer a kijelző adatainak tárolására (128x64 / 8 = 1024 bájt)
uint8_t display_buffer[WIDTH * HEIGHT / 8];
// Elforgatott betűtípus tömbje (futásidőben töltjük fel)
static uint8_t rotated_font_8x8[256 * 8];

// SH1106 parancs küldése
void sh1106_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // 0x00: vezérlőbájt parancsokhoz
    i2c_write_blocking(i2c1, SH1106_ADDR, buf, 2, false);
}

// SH1106 adat küldése
void sh1106_write_data(uint8_t *data, size_t len) {
    uint8_t buf[1 + len];
    buf[0] = 0x40; // 0x40: vezérlőbájt adatokhoz
    for (size_t i = 0; i < len; i++) {
        buf[i + 1] = data[i];
    }
    i2c_write_blocking(i2c1, SH1106_ADDR, buf, 1 + len, false);
}

// Kijelző inicializálása
void sh1106_init() {
    for (size_t i = 0; i < sizeof(init_sequence); i++) {
        sh1106_write_cmd(init_sequence[i]);
    }
    sleep_ms(100); // Rövid késleltetés a parancsok között
    // Puffer törlése
    memset(display_buffer, 0, sizeof(display_buffer));
    rotate_font(); // Betűtípus forgatása
}

// Pixel beállítása a pufferben
void sh1106_set_pixel(int x, int y) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        display_buffer[x + (y / 8) * WIDTH] |= (1 << (y % 8));
    }
}

void sh1106_reset_pixel(int x, int y) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        display_buffer[x + (y / 8) * WIDTH] &= ~(1 << (y % 8));
    }
}

// Kijelző törlése 
void sh1106_clear() {
    memset(display_buffer, 0, 1024);
}


// 4x4-es kocka rajzolása egy bithez
void draw_block(int x, int y, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            sh1106_set_pixel(x + i, y + j);
        }
    }
}
void draw_block_reset(int x, int y, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            sh1106_reset_pixel(x + i, y + j);
        }
    }
}

// Két bájt bitjeinek kirajzolása 4x4-es kockákkal
void draw_bytes(uint8_t byte1, uint8_t byte2, int start_x, int start_y) {
    const int block_size = 7; // 4x4-es kockák
    const int spacing = 1;    // Kockák közötti távolság

    // Első bájt bitjei (0-7)
    for (int i = 0; i < 8; i++) {
        if (byte1 & (1 << i)) {
            draw_block(start_x + i * (block_size + spacing), start_y, block_size);
        }
        else {
            draw_block_reset(start_x + i * (block_size + spacing), start_y, block_size);
        }
    }
    // Második bájt bitjei (8-15)
    for (int i = 0; i < 8; i++) {
        if (byte2 & (1 << i)) {
            draw_block(start_x + (8 + i) * (block_size + spacing), start_y, block_size);
        }
        else {
            draw_block_reset(start_x + (8 + i) * (block_size + spacing), start_y, block_size);
        }
    }
}

// Kijelző frissítése a puffer tartalmával
void sh1106_update() {
    // Oszlop és oldal tartomány beállítása
    sh1106_write_cmd(0x21); // Memóriacímzési mód
    sh1106_write_cmd(0x00); // Vízszintes címzés
    sh1106_write_cmd(0x7F); // Végső oszlop (127)
    sh1106_write_cmd(0x22); // Oldal tartomány
    sh1106_write_cmd(0x00); // Kezdő oldal
    sh1106_write_cmd(0x07); // Végső oldal (7)
    sh1106_write_data(display_buffer, sizeof(display_buffer));
}


// Betűtípus forgatása inicializáláskor
void rotate_font() {
    for (int c = 0; c < 256; c++) { // Minden karakter
        const uint8_t *font = &console_font_8x8[c * 8];
        uint8_t *rotated = &rotated_font_8x8[c * 8];
        memset(rotated, 0, 8); // Nullázás

        for (int y = 0; y < 8; y++) { // Sorok a betűtípusban
            uint8_t row = font[y];
            for (int x = 0; x < 8; x++) {
                if (row & (1 << (7 - x))) { // MSB-től LSB-ig
                    rotated[x] |= (1 << y); // Sor->oszlop
                }
            }
        }
    }
}

// Egy karakter kirajzolása az elforgatott betűtípussal
void draw_char(char c, int start_x, int start_y) {
    if (start_x < 0 || start_x + 8 > WIDTH || start_y < 0 || start_y + 8 > HEIGHT) {
        return; // Kijelző határain kívül
    }

    uint8_t ascii_index = (uint8_t)c;
    const uint8_t *font = &rotated_font_8x8[ascii_index * 8]; // Elforgatott karakter

    int page = start_y / 8; // Melyik 8 pixeles sáv
    int offset = start_y % 8; // Eltolás a bájton belül

    if (offset == 0) {
        // Egyszerű eset: közvetlen másolás
        memcpy(&display_buffer[page * WIDTH + start_x], font, 8);
    } else {
        // Eltolás kezelése
        for (int x = 0; x < 8; x++) {
            uint32_t buffer_index = page * WIDTH + start_x + x;
            uint16_t data = font[x] << offset;
            display_buffer[buffer_index] |= (data & 0xFF); // Alsó bájt
            if (buffer_index + WIDTH < sizeof(display_buffer)) {
                display_buffer[buffer_index + WIDTH] |= (data >> 8); // Felső bájt
            }
        }
    }
}

// Szöveg kirajzolása
void draw_text(const char *text, int start_x, int start_y) {
    int x = start_x;
    while (*text) {
        draw_char(*text, x, start_y);
        x += 8; // Következő karakter (8 pixel széles)
        text++;
        if (x + 8 > WIDTH) break; // Ne menjünk ki a kijelzőből
    }
}
