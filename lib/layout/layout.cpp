#include "layout.h"

int prefixColumns(int x, int w, int margin, int screenW) {
    if (w <= 0 || screenW <= 0) return 0;
    if (x < 0) x = 0;
    if (margin < 0) margin = 0;

    // long: x+w+margin com valores absurdos estouraria int, e o grampeamento
    // logo abaixo so funciona se a soma sobreviver ate ele.
    const long fim = (long)x + w + margin;
    if (fim <= 0) return 0;
    if (fim >= screenW) return screenW;
    return (int)fim;
}

int rowWidth(const int *widths, int count, int gap) {
    if (!widths || count <= 0) return 0;
    if (gap < 0) gap = 0;

    int total = 0, vistos = 0;
    for (int i = 0; i < count; i++) {
        if (widths[i] <= 0) continue;     // slot vazio nao ocupa nem vao
        if (vistos) total += gap;
        total += widths[i];
        vistos++;
    }
    return total;
}

int rowSlotX(const int *widths, int count, int gap, int index, int x0) {
    if (!widths || index <= 0 || index >= count) return x0;
    // A largura de tudo que vem ANTES, com o vao que separa do slot pedido.
    const int antes = rowWidth(widths, index, gap);
    if (antes <= 0) return x0;            // nada antes: comeca na borda
    if (widths[index] <= 0) return x0 + antes;
    return x0 + antes + (gap < 0 ? 0 : gap);
}

int centerIn(int slotX, int slotW, int w) {
    if (w >= slotW) return slotX;
    return slotX + (slotW - w) / 2;
}

int breakAt(const char *txt, int cols) {
    if (!txt) return 0;
    int len = 0;
    while (txt[len]) len++;
    if (cols <= 0) return 0;
    if (len <= cols) return len;

    // Do limite para tras: o ultimo espaco que ainda cabe. `cols` entra na
    // busca de proposito — um espaco EXATAMENTE ali quebra sem sobrar nada
    // pendurado.
    for (int i = cols; i > 0; i--)
        if (txt[i] == ' ') return i;
    return cols;
}
