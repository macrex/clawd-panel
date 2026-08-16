#pragma once

// ---- Display AXS15231B (QSPI) ----
#define TFT_CS    45
#define TFT_SCK   47
#define TFT_SDA0  21
#define TFT_SDA1  48
#define TFT_SDA2  40
#define TFT_SDA3  39
#define TFT_BL     1

#define PANEL_W  320   // nativo do painel (retrato)
#define PANEL_H  480
#define SCREEN_W 480   // como desenhamos (paisagem)
#define SCREEN_H 320

// ---- Touch AXS15231B (I2C, mesmo CI do display) ----
#define TOUCH_SDA   4
#define TOUCH_SCL   8
#define TOUCH_INT   3
#define TOUCH_ADDR  0x3B

// ---- microSD (SD_MMC 1-bit) ----
// Ordem CONFIRMADA empiricamente sondando as 6 permutacoes dos GPIO 11/12/13:
// clk=12 cmd=11 d0=13 monta o cartao (480MB); as demais falham.
#define SD_CLK  12
#define SD_CMD  11
#define SD_D0   13
