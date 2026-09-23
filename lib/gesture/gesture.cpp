#include "gesture.h"

static int absi(int v) { return v < 0 ? -v : v; }

Gesture GestureDetector::update(bool pressed, int x, int y, uint32_t nowMs) {
    Gesture g;

    if (pressed) {
        if (!active_) {              // borda de descida: inicio do gesto
            active_  = true;
            startX_  = x;
            startY_  = y;
            startMs_ = nowMs;
        }
        lastX_ = x;                  // sempre a posicao mais recente valida
        lastY_ = y;
        return g;                    // None enquanto o dedo esta na tela
    }

    if (!active_) return g;          // ja consumido; nao repete
    active_ = false;                 // borda de subida: fim do gesto

    g.x  = lastX_;
    g.y  = lastY_;
    g.x0 = startX_;
    g.y0 = startY_;

    const uint32_t dur = nowMs - startMs_;
    const int dx = lastX_ - startX_;
    const int dy = lastY_ - startY_;

    // 1) Swipe: deslocamento grande num tempo curto.
    //
    // O EIXO MAIOR decide. Sem essa comparacao, um arrasto diagonal viraria os
    // dois gestos dependendo de qual `if` viesse primeiro — e trocar de pagina
    // quando se queria rolar e o tipo de erro que so aparece com o dedo real,
    // nunca no teste que a gente escreve de proposito na horizontal.
    const bool horiz = absi(dx) >= absi(dy);
    if (horiz && absi(dx) >= minSwipe_ && dur <= maxSwipeMs_) {
        hasLastTap_ = false;         // um swipe interrompe a cadeia de toques
        g.kind = (dx < 0) ? GestureKind::SwipeLeft : GestureKind::SwipeRight;
        return g;
    }
    if (!horiz && absi(dy) >= minSwipe_ && dur <= maxSwipeMs_) {
        hasLastTap_ = false;
        g.kind = (dy < 0) ? GestureKind::SwipeUp : GestureKind::SwipeDown;
        return g;
    }

    // 2) Tap: quase sem deslocamento, e rapido.
    if (absi(dx) <= tapSlop_ && absi(dy) <= tapSlop_ && dur <= maxTapMs_) {
        if (hasLastTap_ && (nowMs - lastTapMs_) <= doubleTapMs_) {
            hasLastTap_ = false;     // zera para o terceiro toque nao encadear
            g.kind = GestureKind::DoubleTap;
        } else {
            hasLastTap_ = true;
            lastTapMs_  = nowMs;
            g.kind = GestureKind::Tap;
        }
        return g;
    }

    // 3) Nem uma coisa nem outra (arrasto curto demais, ou dedo apoiado).
    hasLastTap_ = false;
    return g;
}

Leitura FiltroDeSoltura::update(bool pressed, int x, int y, uint32_t nowMs) {
    Leitura l;
    if (pressed) {
        // O dedo voltou antes do limiar: aquilo foi falha do sensor.
        if (vazias_ > 0) {
            const uint32_t falha = nowMs - vazioDesde_;
            if (falha > maiorFalha_) maiorFalha_ = falha;
            falhas_++;
        }
        apoiado_ = true;
        vazias_  = 0;
        x_ = x;
        y_ = y;
        l.pressed = true;
        l.x = x;
        l.y = y;
        return l;
    }

    l.x = x_;
    l.y = y_;
    if (!apoiado_) return l;

    if (vazias_ == 0) vazioDesde_ = nowMs;
    vazias_++;
    // DUAS leituras vazias E o limiar: com o laco lento (um redesenho de 64 ms
    // entre leituras) uma falha so ja passaria do limiar sozinha.
    if (vazias_ >= 2 && nowMs - vazioDesde_ >= soltaMs_) {
        apoiado_ = false;
        vazias_  = 0;
        return l;
    }
    l.pressed = true;              // ainda e o mesmo dedo, no mesmo lugar
    return l;
}

uint32_t FiltroDeSoltura::colherMaiorFalha(uint32_t &quantas) {
    const uint32_t r = maiorFalha_;
    quantas     = falhas_;
    maiorFalha_ = 0;
    falhas_     = 0;
    return r;
}
