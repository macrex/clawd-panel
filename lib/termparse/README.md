# termparse — o parser ANSI/SGR do herdr-assist

Código de terceiro, **não escrito aqui**. Vem de
[walcew/herdr-assist](https://github.com/walcew/herdr-assist), MIT,
© 2026 NorthernMan54 — a licença viaja junto, em `LICENSE`.

## Por que ele está aqui

O modo terminal desenhava a tela do agente em branco, porque a API entregava
texto cru. Passando a pedir `herdr pane read --format ansi`, a cor chega — e
alguém precisa transformar as sequências SGR num grid que o laço de desenho
consiga percorrer.

O autor escreveu esse parser **em C11 puro, sem LVGL nem ESP-IDF, compilável no
host para teste**. É exatamente a regra desta casa: decisão que dá para testar
no PC nasce em `lib/`. Reescrevê-lo daria o mesmo arquivo com outros nomes.

O mesmo projeto já tinha emprestado a ideia da trava de resolução do pane —
está creditado no topo de `server/terminal.py`.

## O que foi mudado

Só os tetos, em `term_parse.h`, para a tela de 320×480 em pé (52 colunas). A
lógica de parse não foi tocada: mexer nela seria assumir a manutenção de um
código que lá tem teste e história.

| | Lá | Aqui | Por quê |
|---|---|---|---|
| `TERM_MAX_COLS` | 220 | 56 | 52 colunas mais folga |
| `TERM_MAX_LINES` | 48 | 56 | a tela em pé cabe 51 linhas |
| `TERM_MAX_RUNS` | 1536 | 1024 | 56 linhas × ~18 runs medidos |
| `TERM_TEXT_CAP` | 16384 | 6144 | 56 × 56 mais um NUL por run |

O `term_grid_t` é alocado inteiro. Com os tetos originais ele passaria de 40 KB
por instância; aqui ele vive na PSRAM, ao lado do framebuffer de 300 KB.

## O que NÃO veio

`term_view.c` (a visão colorida) é LVGL e não atravessa — este projeto é
Arduino_GFX. Quem desenha o grid aqui é `drawTerminal`, em `src/ui.cpp`.

A barra de teclas do `herdr_ui.c` (`Esc`, `Enter`, `C-c`, teclado virtual) manda
`send_keys` no pane. A API daqui é **somente leitura** por decisão documentada
— ver o comentário da rota `/terminal` em `server/claude_metrics_api.py`. Lá
isso é seguro porque a ponte com o herdr é pareada (`src/pairing.c`); aqui não
há pareamento, e uma tela de terminal que digitasse seria teclado aberto na LAN.

## A redução a ASCII fica na API

O `cardputer/src/ui_term.cpp` deles reduz box-drawing e acentos a ASCII dentro
do firmware, porque lá o texto chega cru. Aqui isso já é trabalho de
`server/texto.py`, que faz o mesmo com uma tabela maior e testada — e o
`server/sgr.py` o aplica sem destruir os escapes.
