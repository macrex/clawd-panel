# Clawd Panel

Um painel de mesa para quem trabalha com o **Claude Code**: uma placa ESP32-S3
com tela de 3,5" que mostra, sem você trocar de janela, quanto sobrou dos
limites de uso, quais agentes estão vivos, o que cada um está fazendo e qual
deles parou esperando você responder uma pergunta.

O terminal já diz isso — mas só quando você olha para ele, e só da sessão que
está na frente. O painel fica ao lado do teclado dizendo o tempo todo, de todas
as sessões, de todas as máquinas.

```
┌──────────────────────────┐
│ 🦀  14:51        ☁ 21°   │   cabeçalho: bicho, relógio e clima
├──────────────────────────┤
│  [🧑][🧑][🧑][🧑]         │   a turma: um bicho por estado de agente
│                          │
│  SESSAO            15%   │   limite de 5 horas
│  ███░░░░░░░░░░░  3:09pm  │
│                          │
│  SEMANA             5%   │   limite de 7 dias
│  █░░░░░░░░░░░░  6d08h    │
│                          │
│  AGENTS 1                │
│  ● meu-repo   WIN  Opus  │   um agente por linha, com a máquina de origem
│                          │
│         ● ○ ○ ○     ha 1s│   quatro páginas, e a idade do dado
└──────────────────────────┘
```

## Como funciona

Não há nuvem no meio, nem serviço de terceiro: tudo acontece na sua rede local.

```
Claude Code (N sessões)
   │
   ├── statusline.cjs ──── POST /ingest ──┐   os NÚMEROS, a cada 10 s
   │                                      │
   └── claude_hook.cjs ─── POST /event ───┤   o ESTADO, quando muda
       SessionStart · UserPromptSubmit    │   (Stop, Notification, SessionEnd)
                                          ▼
                            claude_metrics_api.py  (Python, porta 8787)
                                          │
                          GET /status ────┤   a placa busca a cada 2 s por Wi-Fi
                                          ▼
                              ESP32-S3 + tela 320×480
```

**Duas fontes, com hierarquia clara.** A statusline traz os números (percentual
de contexto, limites, modelo, custo); os hooks trazem o estado (trabalhando,
ocioso, bloqueado, encerrado). Quando os dois discordam, o hook ganha — só ele
sabe *por que* um agente parou.

**Várias máquinas.** Cada PC roda o mesmo servidor e se identifica com uma tag
de três letras. A placa consulta duas APIs (`api.url` e `api.url2` no cartão) e
funde as listas: os agentes do segundo PC aparecem com a tag em violeta. Se o
principal cair, o outro assume o cabeçalho.

## O que dá para ver e fazer

**Quatro páginas**, no toque ou no arrasto lateral, em pé ou deitado:

| página | o que mostra |
|---|---|
| **Limites** | as duas janelas (5 h e 7 dias) com percentual, barra, quanto falta e a hora exata da virada |
| **Contexto** | um cartão por agente: repo, branch, modelo, esforço, percentual de contexto e estado |
| **Clawd** | o bicho grande, o clima e o acervo de animações |
| **Nível** | o XP do dia, as horas de convívio e o fundo que muda a cada faixa de nível |

**Telas que tomam a tela inteira quando algo acontece:**

- **Limite estourado** — o bicho aparece com a camisa do recado; um toque duplo
  dispensa e volta ao painel.
- **Janela reiniciada** — o rodízio de bichos anuncia que a sessão ou a semana
  liberou.
- **Servidor fora** — passados 45 s sem contato, o Clawd dorme na tela com
  `CLAUDE OFFLINE` abaixo dele. Os prazos do rodapé continuam certos: a placa
  passa a contá-los sozinha a partir do último valor bom.

**Responder pelo painel.** Quando um agente para numa pergunta de aprovação, a
pergunta e as opções aparecem na tela. Você toca na opção, ela fica *armada*, e
um segundo toque confirma — a resposta volta pelo `POST /responder` e é digitada
no terminal daquele agente.

**Sem sair da cadeira:**

- `tools/tela.py` traz um PNG do que está na tela da placa agora;
- `tools/atualizar_sprite.py` põe um sprite novo no cartão pela rede, sem abrir
  a placa (a placa baixa, confere CRC-32, grava com `.tmp` + rename e reinicia);
- o ícone na bandeja (`server/tray.pyw`) liga e desliga o servidor.

## O hardware

| peça | o que é |
|---|---|
| placa | Guition **JC3248W535** (ESP32-S3, 16 MB de flash, 8 MB de PSRAM) |
| tela | 3,5", 320×480, controlador **AXS15231B** por QSPI, com touch capacitivo |
| cartão | microSD com `config.json` e as animações `.clw` |

A placa custa pouco e chega com um firmware de demonstração. Nada aqui depende
de modificação física: o cartão sai, recebe os arquivos e volta.

## Instalação

Comece por **[`instalar/README.md`](instalar/README.md)**, que explica os dois
papéis, e siga o runbook do seu caso:

- **[`instalar/MASTER.md`](instalar/MASTER.md)** — montar do zero: servidor,
  cartão, firmware e a placa.
- **[`instalar/SLAVE.md`](instalar/SLAVE.md)** — pôr uma segunda máquina
  (Windows ou macOS) para publicar os agentes dela no painel.

O resumo do caminho feliz:

```bash
# 1. o servidor (só biblioteca padrão do Python)
python server/claude_metrics_api.py

# 2. o cartão: o conteúdo de sdcard/ na raiz do microSD, mais um config.json
#    copiado de config.example.json e preenchido com a sua rede e o IP do PC

# 3. o firmware
pio run -t upload
```

`config.json` fica **fora do repositório** — ele carrega a senha do seu Wi-Fi.
O modelo está em [`config.example.json`](config.example.json).

## Como o projeto está dividido

```
src/      firmware: laço principal, rede, tela, touch, cartão, sprites   (~8,4 mil linhas)
lib/      a lógica pura do firmware, com teste nativo                    (~2,2 mil)
  appconfig/  o config.json do cartão            gesture/  arrasto e toque duplo
  layout/     a grade das duas orientações       metrics/  o /status virando modelo de tela
  neturl/     as URLs irmãs da API               nivel/    XP, faixas e o nível do Clawd
  sprite/     o formato .clw dentro da placa
server/   a API local, os hooks e as ferramentas de PC                   (~10 mil)
test/     13 suítes nativas (Unity) da lógica de lib/                    (~2,9 mil)
tools/    compiladores de sprite, atualização pelo ar, captura de tela   (~6,7 mil)
          e o Sprite Studio (tools/sprite_studio, editor de .clw no navegador)
instalar/ os runbooks de instalação, passo a passo
sdcard/   o espelho do cartão: as animações que a placa lê
sprites/  a matéria-prima da arte: folhas de pose e fundos
```

**A regra que separa `src/` de `lib/`:** o que depende do Arduino fica em
`src/`; o que é aritmética pura vai para `lib/` e ganha teste que roda no PC. É
por isso que o parse do `/status`, a fusão de duas máquinas, a regra de nível e
o cálculo de prazo têm teste — e a rotina que desenha um pixel não tem.

## O formato `.clw`

Animação de sprite feita para caber num microcontrolador: RGB565, um quadro
atrás do outro, comprimido por corridas `(cor, repetições)`. O cabeçalho tem 20
bytes e diz quantos quadros, o tamanho, a cor transparente e o milissegundo de
cada quadro.

```
CLWD | versão | quadros | largura | altura | chave | frame_ms | escala | (reservado)
     └─ tabela de offsets (quadros+1) ──┘ └─ corridas (cor, repetições) ─┘
```

A implementação de referência é `tools/clw_format.py` (stdlib pura, usada pelo
compilador, pelos testes e pelo Sprite Studio); a da placa está em
`lib/sprite/`, e decodifica direto para o buffer da tela.

## Desenvolvimento

```bash
pio run                    # compila o firmware
pio test -e native         # os testes que rodam no PC (sem placa)
cd server && python -m pytest -q     # os testes da API
```

Os testes nativos precisam de um compilador de host. Se você já tem `gcc` no
PATH, funciona direto; se não (o caso de uma máquina Windows que só tem os
cross-compilers do ESP32), ponha um [w64devkit](https://github.com/skeeto/w64devkit)
portátil em `toolchain/` — a pasta fica fora do git, e o
`tools/native_toolchain.py` a coloca na frente do PATH da build quando existe.

Windows é o ambiente principal (a placa enumera três dispositivos com o mesmo
VID/PID e o script de gravação sabe disso); o servidor roda igual em macOS, e o
runbook do slave cobre os dois.

## licença

A arte do caranguejo vem do [clawd-tank](https://github.com/marciogranzotto/clawd-tank),
de Marcio Granzotto Rodrigues, sob MIT. Os créditos completos, a medição de
autoria e o que **não** está neste repositório estão em [NOTICE.md](NOTICE.md).
