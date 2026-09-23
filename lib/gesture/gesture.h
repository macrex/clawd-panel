#pragma once
#include <cstdint>

// Os verticais nasceram com a tela de terminal, que e a primeira coisa neste
// painel com mais conteudo do que cabe. Nas paginas de status eles nao existem:
// o laco principal so os consulta em modo terminal, e lá em cima um arrasto
// vertical continua sendo "nem swipe nem tap", como sempre foi.
enum class GestureKind { None, SwipeLeft, SwipeRight, SwipeUp, SwipeDown,
                         Tap, DoubleTap };

struct Gesture {
    GestureKind kind = GestureKind::None;
    int         x    = 0;   // ultima posicao valida do dedo, para hit-test
    int         y    = 0;
    // Onde o dedo POUSOU. Um arrasto termina longe de onde comecou, e a origem
    // e o que diz se ele nasceu no cabecalho (o painel de ajustes) ou no miolo.
    int         x0   = 0;
    int         y0   = 0;
};

// O TOQUE DESTE PAINEL PISCA. Com o dedo parado, o AXS15231B responde "um
// ponto" e "nenhum ponto" em leituras alternadas — medido na placa, 22/09: o
// dedo apoiado em (456,225) virou Tap, DoubleTap, Tap, DoubleTap..., um a cada
// ~9 ms, e 414 "toques" num minuto renderam 7 swipes. Na pagina de contexto
// cada DoubleTap trocava o agente; na do Clawd, o bicho. E os arrastos saiam
// picotados, curtos demais para virar swipe.
//
// O filtro fica ENTRE o sensor e todo o resto: "nenhum ponto" so vira dedo
// levantado quando dura. Antes disso o dedo continua apoiado onde estava.
// Levantar de verdade custa `soltaMs` a mais para ser percebido, o que nao
// muda nada num toque (400 ms de teto) nem num swipe (1 s).
struct Leitura {
    bool pressed = false;
    int  x = 0;
    int  y = 0;
};

class FiltroDeSoltura {
public:
    // ponytail: o limiar e o botao de calibrar. Abaixo dele um duplo toque
    // muito rapido vira um toque so; acima, uma falha longa do sensor no meio
    // do arrasto corta o swipe. O `pulso` publica a maior falha coberta.
    explicit FiltroDeSoltura(uint32_t soltaMs = 70) : soltaMs_(soltaMs) {}

    Leitura update(bool pressed, int x, int y, uint32_t nowMs);

    // A maior falha coberta desde a ultima leitura, em ms, e quantas foram.
    // Zera ao ler: e o numero que diz se o limiar esta certo.
    uint32_t colherMaiorFalha(uint32_t &quantas);

private:
    uint32_t soltaMs_;
    bool     apoiado_    = false;
    int      x_ = 0, y_ = 0;
    int      vazias_     = 0;     // leituras "nenhum ponto" seguidas
    uint32_t vazioDesde_ = 0;
    uint32_t maiorFalha_ = 0;
    uint32_t falhas_     = 0;
};

// Classifica gestos a partir de amostras do touch.
//
// A posicao passada quando `pressed` e false e IGNORADA: drivers de touch nao
// reportam posicao no momento em que o dedo levanta (devolvem um ponto vazio).
// O detector guarda a ultima posicao valida enquanto o dedo esta apoiado.
//
// Um unico detector para todos os gestos, de proposito: dois detectores
// separados disputariam o mesmo evento de release e se contradiriam.
class GestureDetector {
public:
    explicit GestureDetector(int minSwipe = 60, uint32_t maxSwipeMs = 1000,
                             int tapSlop = 20, uint32_t maxTapMs = 400,
                             uint32_t doubleTapMs = 400)
        : minSwipe_(minSwipe), maxSwipeMs_(maxSwipeMs),
          tapSlop_(tapSlop), maxTapMs_(maxTapMs), doubleTapMs_(doubleTapMs) {}

    // Chamar a cada leitura do touch. Devolve o gesto uma unica vez, no
    // instante em que o dedo levanta.
    Gesture update(bool pressed, int x, int y, uint32_t nowMs);

    // O ultimo toque ja foi consumido por algo que mudou a tela (fechou o
    // painel de ajustes): o proximo toque rapido nao pode virar o SEGUNDO de
    // uma dupla, porque cairia na pagina que acabou de aparecer.
    void esquecerToque() { hasLastTap_ = false; }

private:
    int      minSwipe_;
    uint32_t maxSwipeMs_;
    int      tapSlop_;
    uint32_t maxTapMs_;
    uint32_t doubleTapMs_;

    bool     active_     = false;
    int      startX_ = 0, startY_ = 0;
    int      lastX_  = 0, lastY_  = 0;
    uint32_t startMs_    = 0;
    bool     hasLastTap_ = false;   // sentinela: 0 seria um instante valido
    uint32_t lastTapMs_  = 0;
};
