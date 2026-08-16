---
name: jc3248w535
description: Use ao desenvolver firmware para a placa Guition JC3248W535 / JC3248W535EN (ESP32-S3, tela 3.5" 320x480 AXS15231B QSPI com touch). Use tambem ao ver tela apagada e serial muda depois de gravar, esptool com "No serial data received", placa sem acesso aos botoes BOOT/RESET, "esp32-hal-periman.h: No such file or directory", cores BLACK/RED nao declaradas, metade direita da tela cortada, swipe funcionando so num sentido, ou ao montar o cartao SD dessa placa.
---

# Guition JC3248W535 (ESP32-S3 3.5")

Placa de 3.5" com ESP32-S3, tela 320×480 e touch — ambos no **mesmo CI**, o
AXS15231B (display por QSPI, touch por I2C). Costuma vir em case fechada, sem
acesso aos botões, o que torna a gravação o primeiro problema a resolver.

Todos os valores abaixo foram verificados no hardware, não tirados de datasheet.

## Especificação

| Item | Valor |
|---|---|
| SoC | ESP32-S3 (QFN56), rev v0.2 |
| PSRAM | 8 MB octal (`AP_3v3`) |
| Flash | 16 MB, modo **DIO**, fabricante 0x68 (Boya) |
| Tela | 3.5" IPS 320×480 (retrato nativo), AXS15231B, QSPI |
| Touch | capacitivo, mesmo CI, I2C endereço `0x3B` |
| USB | USB-Serial/JTAG **nativo** (VID `303A` / PID `1001`), sem CH340 |
| Extras | microSD, amplificador de áudio, carga LiPo |

```c
// Display QSPI              // Touch I2C
#define TFT_CS   45          #define TOUCH_SDA  4
#define TFT_SCK  47          #define TOUCH_SCL  8
#define TFT_SDA0 21          #define TOUCH_INT  3
#define TFT_SDA1 48          #define TOUCH_ADDR 0x3B
#define TFT_SDA2 40
#define TFT_SDA3 39          // microSD em SD_MMC 1-bit:
#define TFT_BL    1          #define SD_CLK 12
                             #define SD_CMD 11
                             #define SD_D0  13
```

O SD usa **três** pinos porque é SD_MMC 1-bit (CLK/CMD/D0). SPI exigiria quatro.
Das 6 permutações possíveis de 11/12/13, só `clk=12 cmd=11 d0=13` monta.

## Gravar sem tocar em botão

Use **esptool 5.x** (o 4.x não tem `usb-reset`/`watchdog-reset`):

```powershell
esptool --chip esp32s3 -p COM6 --before usb-reset --after watchdog-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB `
    0x0     bootloader.bin `
    0x8000  partitions.bin `
    0xe000  boot_app0.bin `
    0x10000 firmware.bin
```

As duas metades importam e por motivos diferentes:

- **`--before usb-reset`** entra em modo download por software. Sem isso o
  esptool tenta o reset por RTS, a tela pisca (a placa reinicia) mas ela volta
  no app, e você vê `No serial data received`.
- **`--after watchdog-reset`** sai do modo download. Com `hard-reset` o chip
  **reinicia de volta no bootloader** — o flag de download fica latchado no
  USB-Serial/JTAG. O app nunca roda.

## "A tela ficou apagada e a serial está muda"

Sintoma clássico e enganoso: parece falha de display ou de PSRAM, e quase sempre
não é. Faça o teste binário **antes** de mexer em qualquer código gráfico:

```powershell
esptool --chip esp32s3 -p COM6 --before no-reset --after no-reset chip-id
```

- **Conectou** → o chip está parado no bootloader, **o app nunca rodou**.
  Corrija a gravação (`--after watchdog-reset`), não o display.
- **Não conectou** → o app está rodando; aí sim investigue display ou serial.

Para sair do bootloader sem regravar:

```powershell
esptool --chip esp32s3 -p COM6 --before no-reset --after watchdog-reset chip-id
```

## Serial

O `Serial` sai pelo USB-Serial/JTAG nativo. Depois de um reset a porta
re-enumera, então **mensagem única no `setup()` se perde** antes de o host
conseguir abrir a porta. Imprima em `loop()` de forma contínua enquanto estiver
diagnosticando.

### `Serial.printf` BLOQUEIA quando ninguém está lendo

```cpp
Serial.begin(115200);
Serial.setTxTimeoutMs(0);   // sem isto, printf espera o buffer esvaziar
```

Com USB CDC nativo, se o host não tem a porta aberta, o buffer de transmissão
enche e a escrita **espera**. Medido nesta placa: **4 segundos** de `loop()`
parado, causados só por logs.

A armadilha é a assimetria: **só acontece com o monitor fechado.** Quem está
diagnosticando está com o monitor aberto, drenando o buffer, e vê tudo normal —
o travamento existe exatamente na condição em que ninguém está olhando. Custou
uma tarde: a instrumentação adicionada para caçar travamentos virou a maior
causa de travamento, e o sintoma reportado foi "piorou depois que você mexeu".

`setTxTimeoutMs(0)` descarta o texto que não couber. Perder log é irrelevante;
perder o toque por segundos não é. **Ponha no `setup()` de qualquer projeto
desta placa**, antes do primeiro `print`.

## Toolchain

O platform oficial `espressif32@^6.9.0` entrega **Arduino core 2.0.17**, que não
tem `esp32-hal-periman.h` — o Arduino_GFX atual não compila. Use pioarduino:

```ini
[env:board]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = dio_opi   ; flash DIO + PSRAM octal
board_build.flash_mode = dio
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB
build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps = moononournation/GFX Library for Arduino@^1.5.0
```

No core 3.x o LEDC é endereçado **por pino**: `ledcAttach(pin, freq, res)` e
`ledcWrite(pin, duty)`. `ledcSetup`/`ledcAttachPin` não existem mais.

## Display

**TFT_eSPI não funciona** — não suporta painel QSPI. Use `Arduino_GFX` ≥ 1.5.0
ou `esp_lcd` do ESP-IDF.

O painel não rotaciona em hardware. Para desenhar em paisagem, o canvas recebe as
dimensões **nativas do painel** e a rotação age só no espaço de desenho:

```cpp
auto *bus   = new Arduino_ESP32QSPI(TFT_CS, TFT_SCK, TFT_SDA0, TFT_SDA1, TFT_SDA2, TFT_SDA3);
auto *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
auto *cv    = new Arduino_Canvas(320, 480, panel, 0, 0, 1);  // NAO (480,320)
// cv->width() == 480, cv->height() == 320
```

`Arduino_Canvas::flush()` envia o framebuffer **cru**, sem rotação, então ele
precisa casar com o painel. Passar `(480,320)` compila e roda, mas o espaço de
desenho vira 320 de largura e **tudo à direita de x=320 é cortado em silêncio** —
o sintoma é "só metade da tela aparece".

Ligue o backlight **antes** de inicializar o canvas: se o canvas falhar, a tela
ao menos acende e prova que a placa está viva.

Confirme o framebuffer na PSRAM: a livre deve cair ~309.868 bytes (480×320×2).

### O painel IGNORA janela de endereço parcial

**Nenhum retângulo arbitrário pode ser atualizado nesta placa.** Um desenho em
posição qualquer custa o quadro inteiro: **48 ms medidos** só de envio a 40 MHz,
ou ~64 ms contando o redesenho da interface no canvas antes dele.

> **Mas há uma exceção que vale ouro:** as **N primeiras linhas** podem ser
> enviadas sozinhas, justamente porque tudo cai na origem. Ver "A única escrita
> parcial que este painel aceita: o prefixo", na seção de sprites. Não pare
> nesta seção achando que não há saída.

`Arduino_TFT::draw16bitRGBBitmap(x, y, ...)` envia `CASET`/`RASET` corretamente
e mesmo assim o conteúdo cai sempre na origem. Não adianta corrigir coordenada.

**Por que isso engana:** `Arduino_Canvas::flush()` sempre escreve o quadro
inteiro a partir de (0,0), e nessa condição uma janela ignorada produz
exatamente o mesmo resultado de uma janela respeitada. O defeito fica invisível
até alguém tentar escrever num retângulo — e aí parece erro de cálculo de
posição, não do painel.

**O teste que decide, em 5 minutos.** Blite quatro blocos de cores diferentes em
coordenadas **cruas do painel**, uma em cada canto, e fotografe:

```cpp
uint16_t buf[44*44];
for (auto &p : buf) p = RGB565(255,0,0);
panel->draw16bitRGBBitmap(0,       0,       buf, 44, 44);   // vermelho
panel->draw16bitRGBBitmap(320-44,  0,       buf, 44, 44);   // etc.
panel->draw16bitRGBBitmap(0,       480-44,  buf, 44, 44);
panel->draw16bitRGBBitmap(320-44,  480-44,  buf, 44, 44);
```

Quatro blocos em quatro cantos = janela funciona. **Todos no mesmo ponto,
alternando de cor = o painel ignora a janela.**

Não confie em derivar o mapeamento tela↔painel do código do **touch** nem da
fonte do `Arduino_Canvas`: os dois descrevem o caminho *do canvas*, e nenhum
testa o blit direto no painel. Confirmar nos dois e chamar de prova custou três
rodadas de conserto errado.

### Não aumente o clock do QSPI

`cv->begin(80000000)` derruba o flush de 69 para 53 ms — e a imagem fica
**visivelmente pixelada**. Este painel não aceita o clock dobrado. Fique nos
40 MHz padrão (`cv->begin()`).

### Cores

Arduino_GFX usa o prefixo **`RGB565_`**: `RGB565_BLACK`, `RGB565_WHITE`,
`RGB565_RED`, `RGB565_GREEN`, `RGB565_BLUE`, `RGB565_YELLOW`, `RGB565_DARKGREY`.
Os nomes curtos não existem e o erro é confuso (`'RED' was not declared in this
scope; did you mean 'RER'?`). Para cores próprias use a macro `RGB565(r,g,b)`.

As fontes embutidas só têm **ASCII** — acentos e blocos Unicode saem como lixo.
Para arte feita de blocos (`▐▛█▜▌▝▘`, U+2580–U+259F), decomponha cada caractere
nos seus 4 **quadrantes** e desenhe com `fillRect`.

Ao portar arte de terminal, cada quadrante deve ser desenhado com o **dobro da
altura da largura**. Uma célula de caractere no terminal é ~1:2, então seus
quadrantes também são; desenhá-los quadrados deixa o resultado 2× mais largo do
que deveria — visualmente achatado.

## Touch

Protocolo: escreve 8 bytes de comando, lê 8 bytes.

```cpp
const uint8_t READ_CMD[8] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08};
// sem toque: d[0] != 0 || d[1] == 0
int px = ((d[2] & 0x0F) << 8) | d[3];   // 0..319 (retrato)
int py = ((d[4] & 0x0F) << 8) | d[5];   // 0..479
// para canvas em paisagem (rotacao 1):
int x = py;
int y = 319 - px;
```

**O driver não reporta posição no release.** Quem consome precisa guardar a
última posição válida com o dedo apoiado; usar o `x` do release (que vem 0) faz
todo gesto horizontal parecer "para a esquerda".

## Responsividade: o que bloqueia o `loop()`

O toque só é lido quando o `loop()` roda. **A métrica que importa não é "quanto
custa desenhar", é quanto tempo o toque fica sem ser lido** — é esse intervalo
que engole um gesto inteiro. Acima de ~100 ms começa a perder swipe rápido.

Custos medidos nesta placa:

| Bloco | Custo | Onde deve rodar |
|---|---|---|
| Requisição HTTP (1,4 KB, LAN) | **170–250 ms** | tarefa no núcleo 0 |
| Redesenho completo (desenhar + enviar) | ~64 ms | `loop()` |
| Envio da tela inteira, sozinho | 48 ms | `loop()` |
| Envio de prefixo, por coluna | ~0,1 ms | `loop()` |
| Envio de prefixo de 92 colunas | 9 ms | `loop()` |
| Leitura do touch (I²C) | 1 ms | `loop()` |
| `Serial.printf` sem monitor | até 4000 ms | nunca (ver `setTxTimeoutMs`) |

**A rede é o maior bloqueio, e por muito.** 170–250 ms para 1,4 KB na LAN não é
culpa do servidor — medido, o mesmo endpoint responde em 2–14 ms a partir do PC.
É a pilha Wi-Fi do ESP32. Dentro do `loop()`, isso cega o toque a cada poll.

O Arduino roda o `loop()` no **núcleo 1**; o núcleo 0 fica ocioso. Ponha a rede
lá:

```cpp
xTaskCreatePinnedToCore(tarefaRede, "rede", 8192, nullptr, 1, nullptr, 0);
```

A tarefa busca, estaciona o resultado sob um mutex, e o `loop()` só colhe sem
bloquear (`xSemaphoreTake(mtx, 0)`). Pilha de 8 KB: `HTTPClient` e o parser de
JSON alocam bastante. Resultado medido: lacuna máxima de **274–354 ms para
86–92 ms**.

Duas linhas que valem sozinhas:

```cpp
WiFi.setSleep(false);   // modem sleep faz cada transacao pagar o despertar
http.setReuse(true);    // sem isto, TCP novo a cada poll
```

Se precisar animar algo, lembre que **todo quadro custa um flush inteiro** (ver
Display). Pause a animação enquanto houver dedo na tela: perder quadros é
invisível, perder o gesto não.

## Sprites e animação

Como o painel não aceita janela parcial, **todo quadro de animação custa um
flush inteiro** — 64 ms, independente do tamanho do sprite. Isso muda o projeto
inteiro: aqui, animar é caro por natureza, e a pergunta certa não é "como
otimizo o desenho" e sim "preciso mesmo animar isto?".

### Desenhe no canvas, com cor-chave

```cpp
// O buffer sai da decodificação com a própria cor-chave nos vazios.
g->draw16bitRGBBitmapWithTranColor(x, y, buf, chave, w, h);
```

O canvas cuida da rotação — não gire os pixels você mesmo. Se estiver
convertendo arte com compressão RLE, **não gire no conversor**: o RLE comprime
ao longo das linhas, e girar transforma corridas horizontais (que comprimem até
15×) em verticais (que não comprimem nada). Gire na decodificação, onde o índice
de destino já ia ser calculado de qualquer forma.

### Indicador permanente: primeiro tente pôr ele na faixa esquerda

A ordem de preferência, da melhor para a pior:

1. **Anime, com o elemento posicionado na faixa esquerda.** Com o flush de
   prefixo o quadro custa ~9 ms em vez de ~64 (ver a seção do prefixo abaixo).
   Isso é bom o bastante para animar em todas as páginas sem o usuário sentir —
   confirmado no hardware: o swipe continua respondendo com a animação rodando.
2. **Se o elemento não puder ficar na faixa,** mostre um **quadro parado** e
   troque-o quando o estado mudar. A troca já provoca um redesenho, então o
   indicador muda de graça e ainda dá impressão de vida.

O caminho 2 já foi a única saída aqui, e custou uma versão do painel com o
rodapé estático. Um indicador animado a 4 fps por redesenho completo custa ~26%
de CPU permanentes; na faixa, o mesmo indicador a 8 fps custa ~7%.

Se escolher o quadro parado, pegue o de **maior área opaca**, e não o quadro 0 —
em animações de personagem, o quadro 0 costuma ser a pose de repouso, que é a
mais vazia.

**O ponto de projeto:** decida onde o elemento animado vai morar *antes* de
desenhar a tela. Mover um indicador para a faixa esquerda é trivial no papel e
caro depois que a interface está pronta em volta dele.

### Reduzir pixel-art funciona, com ressalva

Vizinho-mais-próximo em arte chunky aguenta bem /2 e /3 e mantém sprites
distinguíveis entre si. **Confira renderizando fora da placa antes de escrever o
firmware** — é rápido e evita gravar para descobrir que ficou ilegível.

O divisor é inteiro, então o tamanho-alvo é **grosso**: aumentar o limite só
muda algo quando algum sprite troca de divisor. Tabule antes de escolher um
número, senão você aumenta o limite e nada acontece.

### Para virar ícone, recorte antes de reduzir

Reduzir o **quadro inteiro** dá um ícone quase vazio. O recorte automático de
uma folha de sprites dimensiona o quadro pela união de todos os frames, então
ele reserva espaço para balão, varinha, faísca e "Z" de sono — coisas que
aparecem em poucos quadros. Medido no `wizard.clw`: quadro 72×129, personagem só
nas linhas 63–119. Reduzido pelo quadro, dois terços do ícone eram vazio.

Duas regras que **não** resolvem sozinhas:

- **Caixa dos pixels opacos do melhor quadro** — as faíscas soltas esticam a
  caixa até o topo (medido: 67×129, ou seja, o quadro inteiro de volta).
- **Caixa dos pixels estáveis (presentes em ≥60% dos quadros)** — algumas
  faíscas persistem, e a caixa volta a 60×122.

O que funciona: a **maior faixa contígua de linhas** com pixels estáveis. O
personagem é um bloco sólido de dezenas de linhas; o enfeite é uma ilha de
poucas linhas separada por um vão vazio. No wizard: ilha de 22 linhas contra
personagem de 57 — sem empate possível. Resultado 60×57, que a /2 vira 30×28 e
lê perfeitamente num cabeçalho.

Ao amostrar o recorte reduzido, use coordenadas **relativas à caixa**, não ao
quadro: amostrar antes de recortar desloca a grade e escolhe outros pixels
representantes.

### A única escrita parcial que este painel aceita: o prefixo

O painel ignora `CASET`/`RASET` — **toda escrita cai na origem**. Medido com
quatro tarjas de larguras diferentes (30, 45, 60 e 75 linhas) mandadas para as
linhas 40, 130, 235 e 355: as quatro apareceram na linha 0, e só a última ficou
visível.

Isso passa despercebido porque o flush inteiro sempre usa a janela
`(0,0,320,480)`, que é o próprio padrão do painel. Você nunca tem evidência de
que a janela funcione até tentar uma diferente.

A consequência é útil, não só limitante: escrever as **N primeiras linhas** está
sempre certo, porque a origem é onde elas pertencem.

```cpp
// Na rotacao 1: panel_y = lx. As N primeiras linhas do painel sao as N colunas
// mais a ESQUERDA da tela em paisagem.
panel->draw16bitRGBBitmap(0, 0, canvas->getFramebuffer(), PANEL_W, colunas);
```

Medido: **~100 µs por coluna**, contra 48 ms do envio de tela inteira. Um
elemento animado de 92 colunas custa 9 ms em vez de 64 — a diferença entre
"animar é caro demais" e "animar é de graça".

**O preço:** só o que estiver na faixa esquerda anima barato. Vale posicionar o
elemento animado ali de propósito, e comentar isso no código — quem mover o
elemento depois não vai adivinhar que ele estava ali por um motivo.

**Confirme com régua, não a olho.** Pinte listras de 10 px, mande um prefixo
conhecido por cima e conte as listras cobertas. Estimar a borda numa foto me fez
concluir que o envio era curto quando ele era exato — e eu quase desenhei uma
margem em cima de um número inventado.

### Um ícone fixo não precisa do arquivo na memória

Se um sprite entra na tela sempre no mesmo quadro (enfeite de cabeçalho, marca
d'água), decodifique **uma vez** no boot e libere o blob. O `wizard.clw` são
97 KB de PSRAM para 48 quadros dos quais só um é usado; o ícone pronto ocupa
menos de 2 KB. As animações de estado precisam do blob inteiro porque trocam de
quadro — essa não.

### Animação sempre perde para o dedo

```cpp
if (t.pressed) ultimoToqueMs = now;
const bool mexendo = ultimoToqueMs && (now - ultimoToqueMs) < 600;
if (!redraw && !mexendo && tick(now)) redraw = true;
```

Durante os 64 ms de flush o toque não é lido, e um swipe precisa de várias
amostras seguidas. Com a animação rodando, gestos rápidos morriam inteiros
dentro de um quadro. Perder quadros é invisível; perder o gesto não.

E anime **só na página que mostra a animação**. Fora dela, custo zero.

**Mas isto vale só para o quadro CARO.** Um elemento na faixa esquerda custa
~9 ms por quadro, e a 9 ms o gesto atravessa sem sentir — congelar ali seria
visível e não compraria responsividade nenhuma. Medido no hardware: selo animado
no rodapé com swipe respondendo na hora, sem pausa. Aplique a prioridade do dedo
onde o quadro custa dezenas de milissegundos, não onde custa unidades.

## Backup do firmware de fábrica

Vem com um demo multimídia LVGL (clima, MP3, vídeo MJPEG, galeria) que lê os
assets e o WiFi do cartão SD — inclusive `music/wifi.json`, que sai de fábrica
apontando para uma rede de teste chinesa. Gravar seu firmware apaga o demo.

Ler os 16 MB antes:

```powershell
esptool --chip esp32s3 -p COM6 --before usb-reset --after no-reset `
    read-flash 0x0 0x1000000 backup.bin
```

Se a leitura falhar em endereços específicos e sempre nos mesmos, **não é flash
defeituosa**: é bug do stub flasher. Releia só esses trechos com `--no-stub`
(usa o bootloader da ROM, mais lento e confiável). Restaurar:
`esptool write-flash 0x0 backup.bin`.

Partições de fábrica = `default_16MB.csv` do Arduino (app0/app1 de 2 MB + ffat).

## Erros comuns

| Sintoma | Causa real |
|---|---|
| Tela apagada, serial muda | Placa presa no bootloader. Use `--after watchdog-reset`. |
| `No serial data received` no esptool | Faltou `--before usb-reset`, ou esptool 4.x. |
| `esp32-hal-periman.h: No such file` | Arduino core 2.x. Troque para pioarduino. |
| `'RED' was not declared in this scope` | Falta o prefixo `RGB565_`. |
| Metade direita da tela em branco | Canvas criado com 480×320 em vez de 320×480. |
| Gesto só funciona num sentido | Usou o `x` do release, que vem 0. |
| Cartão SD não monta | Ordem errada; é `clk=12 cmd=11 d0=13`, 1-bit. |
| Texto com acento sai como lixo | Fonte embutida é ASCII. |
| Sprite sempre no mesmo canto, ignorando a posição | O painel ignora janela parcial. Desenhe no canvas. |
| Trava por segundos, some com o monitor aberto | `Serial.printf` bloqueando. Falta `setTxTimeoutMs(0)`. |
| Gesto perdido a cada poll | Requisição HTTP dentro do `loop()`. Mova para o núcleo 0. |
| Imagem pixelada após "otimizar" | Clock QSPI acima de 40 MHz. |
| Sprite convertido ficou enorme no arquivo | Girou os pixels no conversor e matou a compressão RLE. |
| Indicador animado come CPU o tempo todo | Mova-o para a faixa esquerda e use flush de prefixo (~9 ms/quadro). Se não der, quadro fixo que troca com o estado. |
| Escrita parcial "não funciona" nesta placa | Só a 2D não funciona. Prefixo de linhas funciona: tudo cai na origem. |
| Selo do sprite quase vazio | Usou o quadro 0; pegue o de maior área opaca. |
