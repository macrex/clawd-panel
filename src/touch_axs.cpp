#include "touch_axs.h"
#include "board_pins.h"
#include "display.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
// Comando de leitura do AXS15231B (protocolo confirmado na implementacao do
// ESPHome): 8 bytes de comando, 8 bytes de resposta.
const uint8_t READ_CMD[8] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08};
}

namespace touch {

void begin() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);
    pinMode(TOUCH_INT, INPUT);
}

TouchPoint read() {
    TouchPoint t;

    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(READ_CMD, sizeof(READ_CMD));
    if (Wire.endTransmission() != 0) return t;

    uint8_t d[8] = {0};
    if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)8) != 8) return t;
    for (uint8_t i = 0; i < 8; i++) d[i] = Wire.read();

    // Sem toque: d[0] != 0 ou d[1] == 0 (numero de pontos).
    if (d[0] != 0 || d[1] == 0) return t;

    const int px = ((d[2] & 0x0F) << 8) | d[3];   // 0..PANEL_W-1 (retrato)
    const int py = ((d[4] & 0x0F) << 8) | d[5];   // 0..PANEL_H-1 (retrato)
    t.rawX = px;
    t.rawY = py;

    // O painel e sempre 320x480 de pe; o que muda e como DESENHAMOS.
    //
    // Em pe (rotacao 0) o toque ja chega no mesmo sistema do desenho e nao ha
    // nada a converter. Deitado (rotacao 1) e a conversao de sempre.
    //
    // Este mapeamento tem que acompanhar display::setRetrato: se ele ficar para
    // tras, o dedo cai a 90 graus de onde encostou — e o sintoma parece toque
    // quebrado, nao orientacao errada.
    if (display::retrato()) {
        t.x = px;
        t.y = py;
    } else {
        t.x = py;
        t.y = (PANEL_W - 1) - px;
    }
    t.pressed = true;

    const int lw = display::telaW(), lh = display::telaH();
    if (t.x < 0) t.x = 0;
    if (t.x >= lw) t.x = lw - 1;
    if (t.y < 0) t.y = 0;
    if (t.y >= lh) t.y = lh - 1;
    return t;
}

}
