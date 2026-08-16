#include "display.h"
#include "board_pins.h"
#include <cstring>

namespace {
Arduino_DataBus *bus   = nullptr;
Arduino_GFX     *panel = nullptr;
Arduino_Canvas  *cv    = nullptr;
bool             emPe  = false;
}

namespace display {

bool begin(uint8_t brightness) {
    // Backlight PRIMEIRO: se o canvas falhar, a tela ainda acende e mostra
    // que a placa esta viva. Acender depois esconderia a causa.
    // Arduino core 3.x: o LEDC e enderecado pelo PINO, nao por canal.
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, brightness);

    bus   = new Arduino_ESP32QSPI(TFT_CS, TFT_SCK, TFT_SDA0, TFT_SDA1, TFT_SDA2, TFT_SDA3);
    panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, PANEL_W, PANEL_H);
    // O canvas recebe as dimensoes NATIVAS do painel (320x480). O flush envia o
    // framebuffer cru, sem rotacao (Arduino_Canvas.cpp L581), entao ele precisa
    // casar com o painel. A rotacao 1 age apenas no espaco de DESENHO, ao estilo
    // Adafruit_GFX: trocar w/h faz o codigo desenhar em 480x320 (paisagem).
    // Passar (480,320) aqui daria um espaco de desenho de 320 de largura e
    // cortaria silenciosamente tudo a direita de x=320.
    cv    = new Arduino_Canvas(PANEL_W, PANEL_H, panel, 0, 0, 1);
    // 40 MHz, o padrao do barramento. Ja testei 80 MHz: o flush caiu de 69 para
    // 53 ms, mas a imagem ficou visivelmente pixelada — este painel nao aceita
    // o clock dobrado. 16 ms nao valem sujeira permanente na tela.
    if (!cv->begin()) return false;

    // A placa liga EM PE, a pedido. Aqui e nao no main: assim ate as telas de
    // boot (INICIANDO, SEM CONFIG) ja nascem na orientacao certa, em vez de
    // aparecerem deitadas por um instante e girarem depois.
    setRetrato(true);

    cv->fillScreen(RGB565_BLACK);
    cv->flush();
    return true;
}

Arduino_Canvas *canvas() { return cv; }
Arduino_GFX    *raw()    { return panel; }
void flush()             { cv->flush(); }

bool retrato() { return emPe; }
int  telaW()   { return emPe ? PANEL_W : SCREEN_W; }
int  telaH()   { return emPe ? PANEL_H : SCREEN_H; }

int    quadroW()     { return PANEL_W; }
int    quadroH()     { return PANEL_H; }
size_t quadroBytes() { return (size_t)PANEL_W * PANEL_H * sizeof(uint16_t); }

bool copiarQuadro(uint8_t *dst, size_t n) {
    if (!cv || !dst || n < quadroBytes()) return false;
    uint16_t *fb = cv->getFramebuffer();
    if (!fb) return false;
    std::memcpy(dst, fb, quadroBytes());
    return true;
}

void setRetrato(bool v) {
    if (!cv) return;
    emPe = v;
    // Rotacao 0 = espaco de desenho 320x480 (o nativo), 1 = 480x320. O
    // framebuffer nao muda de tamanho nem de lugar: `_width * _height` da o
    // mesmo produto, e `flush` manda WIDTH x HEIGHT, que sao as dimensoes do
    // construtor e nao mudam com a rotacao. Girar aqui e de graca.
    cv->setRotation(v ? 0 : 1);
}

void flushPrefix(int n) {
    if (!cv || n <= 0) return;
    // O teto e o painel, nao a tela: `n` esta em linhas de PAINEL, e o painel
    // tem 480 delas nas duas orientacoes. Medir isso contra a largura da tela
    // (que troca de valor ao girar) cortaria o prefixo pela metade em pe.
    if (n > PANEL_H) n = PANEL_H;

    uint16_t *fb = cv->getFramebuffer();
    // Sem framebuffer nao da para mandar prefixo. Cai no envio inteiro: perder
    // desempenho e aceitavel, deixar de desenhar nao e.
    if (!fb) { cv->flush(); return; }

    // x=0 e w=PANEL_W de proposito: nao ha janela para restringir, e o painel
    // ignoraria de qualquer forma. `n` entra como ALTURA em coordenadas de
    // painel — e ali que ele sempre foi medido.
    panel->draw16bitRGBBitmap(0, 0, fb, PANEL_W, n);
}
void setBrightness(uint8_t v) { ledcWrite(TFT_BL, v); }

}
