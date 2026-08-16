# Verificação dos critérios de sucesso

Data: 2026-07-30
Firmware: branch `feat/painel-consumo`, Arduino core 3.3.11, flash 18,4% (1.203.802 bytes)
Hardware: Guition JC3248W535, MAC `28:84:85:47:DA:DC`, COM6

## Testes automatizados

`pio test -e native` — **27 de 27 passando** em ~14 s, sem hardware.

| Suíte | Testes | Cobre |
|---|---|---|
| `test_config` | 6 | campos obrigatórios, defaults, senha vazia, mensagens de erro |
| `test_status` | 6 | resposta real, `branch: null`, cores da API, `limits_fresh`, offline |
| `test_view` | 8 | formatação de idade, `—` para desconhecido, pior nível, saturação de barra |
| `test_swipe` | 7 | ambas as direções, toque curto, arrasto lento, reporte único, regressão do release |

## Critérios do spec

### 1. Boot em menos de 15 s — APROVADO

Ciclo de energia completo (USB desplugado e replugado). Sequência observada:
`INICIANDO` → `CONECTANDO MINHA_REDE` → página de limites com dados reais, dentro do
limite de 15 s.

### 2. Os números batem com a API — APROVADO

Comparação no mesmo instante, entre a tela e `http://localhost:8787/status`:

| | Placa | API |
|---|---|---|
| 5 HORAS | 21% / 4h24m | `session_pct: 21`, `session_resets_hm: "4h24m"` |
| SEMANA | 51% / 2d13h | `week_pct: 51`, `week_resets_dh: "2d13h"` |
| CONTEXT | 39% | `context_pct: 39` |
| repo / branch | esp32-s3 / feat/painel-consumo | idem |

### 3. As cores vêm da API — APROVADO

Com `colors: {context: green, session: green, week: yellow}`, o pill do rodapé
apareceu **amarelo escrito ATENCAO** — a pior das três métricas governando,
sem nenhuma regra de limiar duplicada no firmware.

### 4. Swipe confiável — APROVADO

Alternância entre as duas páginas nos dois sentidos, de forma repetível.
Ressalva de precisão: o teste foi por uso repetido até o operador se convencer,
não uma contagem formal de 10 de 10 como o plano previa.

### 5. A API cai e volta — APROVADO

Processo da API encerrado (`Stop-Process` no PID que escutava a 8787), 25 s fora,
e religado. Comportamento observado:

- A tela **manteve os números** em vez de escondê-los
- Tudo ficou cinza (sem verde/amarelo/vermelho)
- `SEM CONTATO HA Ns` em amarelo no cabeçalho, com o contador subindo
- Voltou ao normal **sozinha** quando a API religou, sem reiniciar a placa

É exatamente o comportamento especificado: dado velho continua informando, mas
nunca se disfarça de fresco.

### 6. O WiFi cai e volta — NÃO TESTADO DELIBERADAMENTE

O operador optou por não derrubar o rádio do roteador. Existe evidência
**acidental**: durante a Task 9 a serial registrou `wifi status=6` (desconectado)
seguido de `wifi status=0` e depois `OK ctx=...` — a placa perdeu e recuperou a
conexão sozinha, sem intervenção. Isso sustenta o critério, mas não o substitui.

**Pendente:** desligar o rádio do roteador por 30 s e confirmar que a tela mostra
`SEM WIFI` e retoma os dados sozinha.

### 7. Estado sem configuração — NÃO TESTADO

O caminho `SEM CONFIG` está implementado e foi exercitado com dados falsos na
Task 10 (a tela renderiza corretamente), mas o cenário real — remover o cartão SD
e reiniciar — não foi executado.

**Pendente:** remover o cartão, reiniciar, confirmar `SEM CONFIG / cartao SD nao
montou`, recolocar e reiniciar.

## Defeitos encontrados e corrigidos

| Onde | Defeito | Como apareceu |
|---|---|---|
| `tools/flash.ps1` | `--after hard-reset` deixava a placa presa no bootloader; o app nunca rodava (tela apagada, serial muda). Corrigido para `--after watchdog-reset`. | Bisecção: nem um firmware mínimo imprimia. `esptool --before no-reset` conectou, provando que o chip estava no bootloader. |
| `src/display.cpp` | Canvas criado com 480×320 fazia o espaço de desenho virar 320 de largura, cortando em silêncio tudo à direita de x=320. Corrigido para as dimensões nativas do painel. | Só o quadrado azul e o texto apareciam; os dois cantos direitos sumiam. |
| `lib/gesture/swipe.cpp` | O driver devolve ponto vazio (`x=0`) no release, então `dx = 0 - startX` era sempre negativo e **todo** gesto virava `Swipe::Left`. | Integração: dava para ir à página 2 e nunca voltar. Os 6 testes unitários passavam porque passavam a posição final no release — algo que driver nenhum faz. |
| `platformio.ini` | `espressif32@^6.9.0` entrega Arduino core 2.0.17, sem `esp32-hal-periman.h`; o Arduino_GFX atual não compila. Corrigido para pioarduino (core 3.3.11), que é o que o spec já dizia. | Erro de compilação no primeiro build da placa. |
| `test_status.cpp` | `"Opus 5 (1M context)"` fecha um raw string comum: a sequência `)"` é o terminador. Corrigido com delimitador nomeado `R"json( )json"`. | Erro de compilação enganoso, apontando para a linha errada. |

## Correções na documentação

- **Firewall:** o spec afirmava que a porta 8787 estava bloqueada e exigia uma
  regra nova. Errado — o Windows já tinha regra **por aplicativo** liberando o
  `pythonw.exe`. A análise original só examinou regras por porta. A placa lê a
  API sem nenhuma regra adicional.
- **Pré-requisitos:** o plano não verificava PlatformIO nem compilador C++ do
  host; nenhum dos dois existia na máquina e ambos bloquearam a execução.

## Descobertas de hardware

- Pinagem do SD confirmada sondando as 6 permutações: **`clk=12 cmd=11 d0=13`**
  em SD_MMC 1-bit. As outras cinco falham.
- Mapeamento do touch validado nos quatro cantos: `(14,21)` `(461,9)` `(14,307)`
  `(461,307)` — a variante alternativa do plano não foi necessária.
- Framebuffer confirmado na PSRAM: livre caiu de 8.388.608 para 8.078.740 bytes,
  exatamente os 309.868 do buffer 480×320×2.
