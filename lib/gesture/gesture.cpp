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

    g.x = lastX_;
    g.y = lastY_;

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
