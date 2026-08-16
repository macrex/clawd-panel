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
