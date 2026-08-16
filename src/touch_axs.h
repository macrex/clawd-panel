#pragma once

struct TouchPoint {
    bool pressed = false;
    int  x = 0;     // ja no espaco do canvas: 0..479
    int  y = 0;     // ja no espaco do canvas: 0..319
    int  rawX = 0;  // cru do painel (retrato), para diagnostico
    int  rawY = 0;
};

namespace touch {
void       begin();
TouchPoint read();
}
