# Claude Metrics API

API local que exporta as métricas da statusline do Claude Code para consumo
na LAN (ESP32-S3 com tela touch).

## Arquitetura

```
Claude Code (N sessões) ──stdin JSON──► statusline.cjs   ── POST /ingest ──┐
   │                                    (os NÚMEROS, a cada 10s)           │
   │                                                                       ▼
   └── hooks de ciclo de vida ────────► claude_hook.cjs  ── POST /event ──► claude_metrics_api.py
       SessionStart · UserPromptSubmit  (o ESTADO, quando muda)             0.0.0.0:8787
       Stop · Notification · SessionEnd                                     dict em memória
                                                                                  │
                                                                     GET /status (polling Wi-Fi)
                                                                                  ▼
                                                                       ESP32-S3 (seu firmware)
```

**Duas fontes, com hierarquia clara.** A statusline traz os números; os hooks
trazem o estado. Os hooks são **autoritativos** — é a mesma hierarquia que o
[herdr](https://herdr.dev/docs/agents/) usa ("a integração é autoritativa quando
está instalada e reportando"), e pelo mesmo motivo: só o hook sabe *por que* um
agente parou.

**Duas visões dos mesmos dados**, porque duas perguntas diferentes precisam ser
respondidas:

1. **Campos de topo — o resumo.** Um número por métrica, para um mostrador
   pequeno que não cabe uma lista.

   | Campo | Origem |
   |-------|--------|
   | `session_pct` / `week_pct` | Globais da conta — da janela de 5h vigente |
   | `context_pct` + `context_repo` + `context_branch` | Da sessão com **maior** context% |
   | `model` | Da sessão que publicou **por último** |
   | `sessions` | Contagem de sessões vivas (publicaram nos últimos 30s) |
   | `herdr_online` | O [herdr](#o-sensor-do-herdr) está respondendo agora? |
   | `done` | Quantos itens de `labels[]` terminaram sem você ver — soma de `done` por item |
   | `motor` | Qual motor decidiu o estado das sessões nesta resposta: `"herdr"` ou `"hooks"` — ver [Os dois motores de estado](#os-dois-motores-de-estado) |

2. **`labels[]` — um agente por entrada.** Cada agente do Claude Code tem o seu
   próprio contexto, repo, branch e modelo. Com vários diretórios abertos, o
   resumo escolhe um e **esconde todos os outros** — inclusive o fato de que
   podem estar rodando modelos diferentes. A lista resolve isso.

   Ordenada por `context_pct` decrescente; agentes que não reportam contexto
   ficam no fim, com `null` (nunca `0`, que seria mentira).

   Os limites de 5h e 7 dias **não** entram em cada entrada: são globais da
   conta, iguais para todos. Ficam no topo, como `session_label` e `week_label`.

3. **`bloqueio` — a pergunta que trava alguém.** `null` no caso normal. Quando
   um agente para esperando aprovação, o campo traz a pergunta dele e as opções
   já detectadas na tela do terminal, prontas para virar botão:

   ```json
   "bloqueio": {"agent_id": "817d452c", "pane_id": "w0:p1", "seq": 3,
                "pergunta": "Do you want to proceed?",
                "opcoes": [{"n": 1, "rotulo": "Yes", "texto": false},
                           {"n": 2, "rotulo": "No, and tell Claude", "texto": true}]}
   ```

   O herdr diz que um agente está `blocked` e **para por aí**: o `agent_status`
   dele é derivado por regra sobre o texto do terminal e não expõe qual é a
   pergunta. Não há API para isso, então quem lê a lista é o parser em
   `bloqueio.py` — portado do [herdr-assist](https://github.com/walcew/herdr-assist)
   (MIT), onde as expressões regulares foram calibradas contra telas reais.

   Três coisas que não são falha e sim caso previsto:

   - **`opcoes: []` com o campo presente** — o agente está bloqueado mas o que
     está na tela dele não é um seletor navegável. O painel mostra a pergunta sem
     botão. Um `Enter` numa tela que não esperava `Enter` responde uma pergunta
     que ninguém fez.
   - **`pergunta: ""`** — não deu para ler a tela. O bloqueio ainda é anunciado:
     sumir esconderia o único estado que exige ação de quem olha.
   - **`texto: true` numa opção** — escolher abre um campo de texto em vez de
     decidir. O toque não resolve o bloqueio, só troca de pergunta.

   `seq` anda quando a **pergunta** muda, e não a cada leitura — é o que o
   firmware usa para desarmar o botão. Um `seq` que andasse sozinho desarmaria o
   botão debaixo do dedo de quem está respondendo.

   A leitura da tela custa ~50 ms de subprocess e o painel pede `/status` a cada
   2 s, então há cache de 5 s. Sem ninguém bloqueado **nada é lido**.

## Endpoints

Base: `http://192.168.0.10:8787` (IP fixo reservado no roteador via DHCP binding)

### `GET /status` — o que o ESP32 consome

```json
{
  "online": true,
  "sessions": 4,
  "sessions_active": 3,
  "blocked": 1,
  "model": "Opus 5 (1M)",
  "effort": "High",
  "context_pct": 50,
  "context_repo": "esp32-s3",
  "context_branch": "main",
  "session_pct": 46,
  "session_resets_in": 13129,
  "session_resets_hm": "3h38m",
  "session_resets_clock": "7:20pm",
  "week_pct": 53,
  "week_resets_in": 217729,
  "week_resets_dh": "2d12h",
  "week_resets_date": "01/08/2026 (Sabado)",
  "updated_ago": 3,
  "limits_fresh": true,
  "session_known": true,
  "week_known": true,
  "session_inferred": false,
  "session_label": "Session 46% 3h38m",
  "week_label": "Week 53% 2d12h",
  "labels": [
    { "session_id": "2bfc2d48", "repo": "outro-repo", "branch": "validacao-arquivos",
      "model": "Opus 5 (1M)", "effort": "High", "context_pct": null, "color": "green",
      "age": 240, "stale": true, "state": "blocked", "state_age": 232,
      "event": "Notification", "agent": "claude", "done": false, "proc_alive": true,
      "line": "❖ outro-repo  ⎇ validacao-arquivos | Opus 5 (1M) High | Context —" },
    { "session_id": "817d452c", "repo": "esp32-s3", "branch": "main",
      "model": "Opus 5 (1M)", "effort": "High", "context_pct": 50, "color": "yellow",
      "age": 4, "stale": false, "state": "working", "state_age": 12,
      "event": "UserPromptSubmit", "agent": "claude", "done": false, "proc_alive": true,
      "line": "❖ esp32-s3  ⎇ main | Opus 5 (1M) High | Context 50%" },
    { "session_id": "91bac378", "repo": "meu-repo", "branch": null,
      "model": "Opus 4.8", "effort": "Medium", "context_pct": 27, "color": "green",
      "age": 3, "stale": false, "state": "idle", "state_age": 95,
      "event": "Stop", "agent": "claude", "done": false, "proc_alive": true,
      "line": "❖ meu-repo | Opus 4.8 Medium | Context 27%" },
    { "session_id": "w2:p3", "repo": "outro-projeto", "branch": null,
      "model": null, "effort": null, "context_pct": null, "color": "green",
      "age": 0, "stale": false, "state": "idle", "state_age": null,
      "event": "herdr", "agent": "codex", "done": true, "proc_alive": null,
      "line": "❖ outro-projeto | Codex | Context —" }
  ],
  "colors": { "context": "yellow", "session": "green", "week": "yellow" },
  "herdr_online": true,
  "done": 1,
  "captura": null,
  "clock": { "time": "16:26", "date": "30/07", "weekday": "QUI", "epoch": 1785439580 },
  "weather": { "temp": 30, "min": 21, "max": 30, "code": 0, "text": "limpo",
               "night": false, "city": "Valparaiso de Goias", "age": 5 }
}
```

O último item de `labels[]` acima é um **órfão do herdr**: um agente que o
herdr vê e o Claude Code não — `agent: "codex"`, sem par por `pane_id` em
nenhuma sessão nossa. Ele tem exatamente as mesmas chaves que os agentes do
Claude Code, para que o firmware desenhe `-`/`—` nos campos que faltam sem
precisar de um caminho de código separado. Ver [O sensor do
herdr](#o-sensor-do-herdr) abaixo.

### As duas janelas são independentes

`session_known` e `week_known`, uma por janela. Elas existem por causa de um
defeito real: havia só `limits_fresh`, e quando a janela de 5h virava a **semana
ia para zero junto** — mesmo estando válida por mais dois dias. Uma janela não
pode mais derrubar a outra, e cada uma escolhe seu próprio snapshot de
referência (nada obriga o melhor registro de 5h e o melhor semanal a virem da
mesma sessão).

`limits_fresh` continua no payload para quem lê o campo antigo, mas agora
significa "as duas vêm de leitura real, sem inferência".

**`session_inferred`.** O `statusline.cjs` só republica limites quando o Claude
Code chama a API. Entre "a janela de 5h venceu" e "alguém usar de novo", todos os
snapshots estão vencidos — e esse intervalo dura até você mandar uma mensagem,
ou seja, pode durar horas. Mostrar `—` nesse buraco escondia o fato mais
acionável que existe: **você está com a cota inteira**.

Quando a janela vence, a API passa a reportar `session_pct: 0` com
`session_inferred: true`. Isso não é palpite: no instante da virada, uma sessão
publicou `session_pct: 0` sozinha enquanto as outras duas ainda carregavam 32% e
40% da janela anterior. Janela fixa vencida = uso recomeçado.

O **prazo**, esse continua desconhecido (`session_resets_hm: "-"`): a próxima
janela só começa quando alguém usar, e inventar um horário seria a mentira que o
zero não é.

O mesmo truque **não vale para a semana**: a janela de 7 dias desliza, ela não
vira com uso zerado. Vencida, ela volta a ser `week_known: false`.

### `model` e `effort` — o que pensa, e com quanta força

O `statusline.cjs` publica `effort` a partir de `data.effort.level` (`low`,
`medium`, `high`, `xhigh`, `max`). A API o devolve **já capitalizado para
exibição** — `high` → `High`, `xhigh` → `XHigh`. O mapeamento mora aqui, e não
no firmware: quem conhece o vocabulário é quem fala com o Claude Code, e `XHigh`
é uma regra que nenhuma capitalização automática acerta.

`effort` aparece **por agente** em `labels[]`, porque cada sessão tem o seu — dá
para ter uma em `High` e outra em `Low` ao mesmo tempo. `null` quando a sessão
publica por uma statusline anterior ao campo.

O `model` sai encurtado: `Opus 5 (1M context)` → `Opus 5 (1M)`. A palavra
"context" não informa nada (o que está entre parênteses num nome de modelo já é
o tamanho da janela) e custa 8 caracteres — a linha do modelo no painel tem 18,
e sem encurtar o nome sozinho já era truncado. O nome cru continua visível em
`/sessions`.

### `clock` e `weather` — o mundo fora do Claude Code

Vêm juntos no mesmo payload de propósito: o painel já pede este endereço a cada
dois segundos, então a hora chega de carona. Uma segunda requisição só para o
relógio seria trabalho duplicado, e — mais importante — mais uma conexão externa
dentro do laço de desenho do firmware.

**Por que não no ESP32.** A placa não tem relógio de bateria: no boot ela não
sabe que dia é. Daria para ela falar NTP e um serviço de clima sozinha, mas
seriam duas conexões a mais no firmware, com TLS, timeout e tratamento de erro.
Aqui o custo é uma thread e trinta linhas.

| Campo | Significado |
|-------|-------------|
| `clock` | Sempre presente. Sai de `time.localtime()` na hora da requisição — nunca é cacheado. |
| `clock.weekday` | `SEG`…`DOM`, **sem acento**: a fonte embutida do Arduino_GFX só tem ASCII, e `SÁB` sairia como lixo na tela. |
| `weather` | `null` até a primeira leitura chegar. Null é diferente de zero grau, e o painel desenha um traço em vez de inventar um número. |
| `weather.text` | Descrição curta e sem acento (`limpo`, `nublado`, `chuva`, `pancadas`). Curta porque o cabeçalho tem ~16 caracteres de largura útil. |
| `weather.age` | Segundos desde a leitura. A API mantém a última boa quando a rede dela cai; sem isto, meia hora de defasagem passaria por atual. |

Fontes: **Open-Meteo** (previsão, sem chave e sem cadastro) e **ip-api.com**
(coordenadas, uma vez só). As coordenadas ficam em `weather.json` ao lado do
script — editável, porque a geolocalização por IP acerta a região e erra o
bairro. Esse arquivo **não** é versionado: é o endereço de quem roda isto.

### Entradas de `labels[]`

| Campo | Tipo | Significado |
|-------|------|-------------|
| `session_id` | string | 8 primeiros caracteres — distingue dois agentes no **mesmo** repo |
| `repo` / `branch` | string\|null | Do diretório daquele agente; `branch` é `null` fora de git |
| `model` | string\|null | O modelo **daquele** agente — pode diferir entre agentes. Encurtado: `(1M context)` → `(1M)` |
| `effort` | string\|null | Esforço daquele agente, pronto para exibir (`High`, `XHigh`). `null` sem o campo |
| `context_pct` | int\|null | `null` quando o agente não reporta contexto |
| `color` | string | `green`/`yellow`/`red` pela mesma regra; `green` quando `context_pct` é `null` |
| `age` | int | Segundos desde a última publicação **desse** agente |
| `state` | string | `blocked` / `working` / `idle` / `unknown` — ver abaixo |
| `state_age` | int\|null | Segundos desde a última mudança de estado |
| `event` | string\|null | Hook que produziu o estado (`Notification`, `Stop`…), ou `"herdr"` para um órfão |
| `agent` | string | Quem é o agente: `"claude"` para toda sessão nossa; `"codex"` / `"gemini"` / `"cursor"` / etc. para um órfão do herdr — ver [O sensor do herdr](#o-sensor-do-herdr) |
| `done` | bool | Terminou e você ainda não viu. Só um órfão do herdr sabe distinguir isto de um `idle` qualquer — é ele quem lê a tela |
| `stale` | bool | `true` quando parou de publicar há mais de 30s. Diz que o heartbeat calou — **não** diz o motivo. Chamava-se `idle`, e o nome mentia: dá para estar ocioso publicando, e trabalhando sem publicar |
| `proc_alive` | bool\|null | O `claude.exe` ainda existe? Informativo apenas; `null` para um órfão (não é processo nosso) |
| `line` | string | Linha pronta para imprimir, no formato da statusline |

Ordenação: `blocked` primeiro, depois maior contexto. Quem precisa de você vai
para o topo da lista.

### `?campos=painel` — o documento sem o que o firmware não lê

A placa busca este endpoint **a cada dois segundos** e reparseia tudo. Medido
contra o payload real com um agente vivo, **450 de 1.917 bytes (23%)** são
chaves que `lib/metrics/status.cpp` nunca indexa: elas atravessam a rede, entram
no parser, ocupam heap e morrem ali. Com mais agentes a fração cresce, porque
`labels[].line` custa ~49 B **por agente** — e é uma statusline em UTF-8 que a
fonte embutida do painel nem consegue desenhar (ela cobre `0x20..0x7E`).

Quem pede o corte é a **placa**, com `?campos=painel`. Sem o parâmetro o
documento sai inteiro, e isso é deliberado:

- `tools/tela.py`, o `curl` de depuração e qualquer consumidor futuro continuam
  vendo o contrato completo;
- uma placa gravada **antes** desta versão não manda o parâmetro e recebe tudo,
  como sempre recebeu. O firmware é versionado junto com a API, mas a placa não
  se atualiza sozinha — e é ela que fica na parede.

O que sai (a lista mora em `painel.py`, com o porquê de cada campo):

| Onde | Chaves |
|---|---|
| topo | `sessions_active`, `blocked`, `session_label`, `week_label`, `herdr_online` |
| `labels[]` | `state_age`, `event`, `proc_alive`, `line` |
| `works` | `api_seconds`, `blocks`, `mediana_gap_seconds`, `marcados` |
| `uso` | `estimados`, `fator`, `vigente`, `dia` |
| `uso.modelos[]` | `tokens`, `estimado` |
| `weather` | `code`, `night` |

**A lista é de remoção, e não de permissão.** Com lista de permissão, um campo
novo que o firmware aprendesse a ler sumiria em silêncio até alguém lembrar de
atualizar a lista, e o sintoma apareceria como um dado faltando na tela, longe
daqui. Com lista de remoção o pior caso é continuar mandando alguns bytes à toa
— que é o problema que isto existe para diminuir, não para criar.

Ao ensinar o firmware a ler um campo, **tire-o da lista**. `test_painel.py`
cobre o corte, e `test/test_enxuto/` (nativo, no firmware) prova que os dois
payloads produzem o **mesmo** `Status` — é lá que um corte errado aparece, e não
na tela dias depois.

### `state` — como se distingue "bloqueado" de "fechado"

O heartbeat só acontece enquanto o Claude Code **renderiza** a statusline. Um
agente bloqueado numa pergunta para de publicar imediatamente. Medido:

```
10:44:34   1s     publicando normalmente
10:44:58   4s
10:45:06  13s     bloqueou: a idade so cresce
10:45:21  27s     a 3s de ser removido
```

Com o TTL antigo de 30s, o agente **sumia da tela justo quando mais precisava de
atenção**. A primeira correção foi paliativa — `TTL_SECONDS = 900` e um flag
`idle` — apoiada na conclusão de que bloqueado e fechado eram indistinguíveis.

**Essa conclusão estava errada.** Ela vinha de olhar só para o heartbeat. Os
hooks de ciclo de vida do Claude Code dizem o estado explicitamente:

| Hook | Vira | Significa |
|------|------|-----------|
| `UserPromptSubmit` | `working` | você mandou um prompt; Claude assumiu |
| `Notification` (`permission_prompt`, `agent_needs_input`, `elicitation_dialog`) | `blocked` | **precisa de você** |
| `Stop` | `idle` | turno acabou; a bola é sua, sem urgência |
| `SessionEnd` | — | removida da lista **imediatamente** |
| nenhum | `unknown` | sessão sem hooks instalados |

Só `blocked` faz o painel pulsar. Antes, qualquer sessão que você deixasse
quieta por 30s pulsava vermelho; agora o vermelho significa uma coisa só.

**Um caso sutil, resolvido de graça.** Você aprova uma permissão e o Claude volta
a trabalhar — mas o próximo hook só dispara no fim do turno (`Stop`). Sem
correção, o agente ficaria pulsando `blocked` enquanto trabalha. A saída sai do
próprio heartbeat: se o `context_pct` **mudou** desde que travou, é porque está
gerando tokens de novo → `working`.

### Por que `pid` não decide nada

`CLAUDE_PID` é uma variável de ambiente que aponta para o `claude.exe`. Parecia
o oráculo perfeito de "fechado vs. bloqueado". **Não é.** Medido em 30/07/2026,
com uma sessão comprovadamente viva e bloqueada:

```
sessoes:  meu-repo=3904  esp32-s3=3420  outro-repo=28708
claude.exe vivos: 3904, 24672, 3420, 25968      <- 28708 NAO esta aqui
```

O `outro-repo` publicou um PID já morto — a statusline às vezes roda num processo
filho transitório. A regra ingênua "PID morto = remover" teria apagado
justamente o agente bloqueado: o pior erro que esta API pode cometer.

Por isso `proc_alive` é **exposto mas nunca decide sozinho**. Ele só remove uma
sessão que já está em silêncio há mais de `DEAD_PID_GRACE` (45s) **e** cujo
estado não é `blocked` — cobrindo queda bruta (janela fechada no X, reboot) sem
nunca ameaçar quem espera por você.

**TTL em duas velocidades:** sessão com hooks vive 12h (confia-se no
`SessionEnd`); sessão sem hooks mantém os 15 min antigos.

`online` passa a significar "alguém publicando **agora**" — agentes ociosos não
sustentam sozinhos essa afirmação. E o resumo do topo sai só de quem está
publicando: um número de 10 minutos atrás não pode virar a manchete.

`sessions` conta todos os vivos (inclusive ociosos); `sessions_active` conta só
os que publicam.

| Campo | Tipo | Significado |
|-------|------|-------------|
| `online` | bool | `false` quando nenhuma sessão publicou nos últimos 30s (Claude Code fechado) |
| `sessions` | int | Quantas sessões vivas |
| `sessions_active` | int | Quantas publicaram nos últimos 30s |
| `blocked` | int | Quantas precisam de você **agora** |
| `model` | string\|null | Nome do modelo (ex: `"Opus 4.8"`) |
| `context_pct` | int 0-100 | Maior uso de janela de contexto entre as sessões |
| `context_repo` | string\|null | Repo da sessão com maior context |
| `context_branch` | string\|null | Branch dessa sessão (`null` fora de git) |
| `session_pct` | int 0-100 | Uso do limite de 5 horas (global da conta) |
| `session_resets_in` | int | Segundos até resetar o limite de 5h |
| `session_resets_hm` | string | Mesmo valor formatado `HhMMm` — pronto pra imprimir |
| `session_resets_clock` | string | O MESMO instante em hora de relógio (`"7:20pm"`), no fuso desta máquina. `"-"` sem carimbo |
| `week_pct` | int 0-100 | Uso do limite de 7 dias (global da conta) |
| `week_resets_in` | int | Segundos até resetar o limite semanal |
| `week_resets_dh` | string | Mesmo valor formatado `DdHHh` |
| `week_resets_date` | string | O MESMO instante em data e dia da semana (`"01/08/2026 (Sabado)"`). `"-"` sem carimbo |
| `updated_ago` | int | Segundos desde a última publicação (frescor) |
| `herdr_online` | bool | O sensor do herdr respondeu dentro do prazo de frescor? Ver [O sensor do herdr](#o-sensor-do-herdr) |
| `done` | int | Quantos agentes (nossos ou órfãos) terminaram sem você ver — soma de `done` em `labels[]` |
| `motor` | string | Qual motor decidiu o estado agora: `"herdr"` ou `"hooks"` — ver [Os dois motores de estado](#os-dois-motores-de-estado) |
| `captura` | obj\|null | Pedido de foto da tela esperando a placa (`{"id": N}`). `null` no caso normal — a chave está sempre presente, como `bloqueio` |

> Os `*_resets_in` são calculados **na hora da leitura** — o contador nunca fica velho,
> mesmo que a sessão não publique há alguns segundos.

**Faixas de cor sugeridas** (mesma regra da statusline):
`< 50%` verde · `50–79%` amarelo · `>= 80%` vermelho

### O sensor do herdr

O [herdr](https://herdr.dev/docs/agents/) é um multiplexador de terminal que
lê a TELA RENDERIZADA e reconhece 19 agentes diferentes — não só o Claude
Code. `server/herdr.py` consulta a CLI dele (`herdr agent list`) numa thread
de fundo, no mesmo ritmo com que o ESP consulta `/status` (`CONSULTA_S = 2s`),
e guarda o resultado num retrato em memória.

**Por que isto existe.** Sem o herdr, o painel só enxerga o que o próprio
Claude Code publica — zero sessões nossas significa tela vazia, mesmo com um
codex ou gemini trabalhando ao lado. Com o herdr, esses agentes aparecem como
**órfãos** em `labels[]` (`agent` diferente de `"claude"`, sem `session_id`
nosso — ver a tabela de `labels[]` acima).

**Casamento por `pane_id`.** Quando uma sessão nossa e um agente do herdr
compartilham o mesmo pane, o herdr **ganha** o estado: ele lê a tela e não
depende de hook nenhum, o que resolve casos em que os hooks do Claude Code não
disparam (Esc, queda do processo). A chave (`herdr_pane_id`) chega por dois
canais — o hook (`claude_hook.cjs`, todo evento de ciclo de vida) e a
statusline (`statusline.cjs`, a cada heartbeat) — porque o segundo é bem mais
frequente e faz uma sessão parada parear mais cedo. Os dois escrevem a mesma
chave, sem heurística nenhuma envolvida.

**Frescor.** `herdr_online` no topo do payload só é `true` quando o retrato
tem no máximo `RETRATO_MAX_IDADE_S` (3× `CONSULTA_S`) de idade. Sem essa
guarda, uma falha silenciosa na thread de consulta congelaria `online: true`
com uma lista cada vez mais velha, e o retrato imóvel passaria a sobrepor a
nossa própria máquina de estados via `fundir()` — o oposto do que este sensor
existe para consertar.

**Divergência.** Quando o herdr e a nossa máquina de estados discordam sobre a
mesma sessão por mais de `DIVERGENCIA_S` (30s), o episódio é registrado — uma
vez por episódio, não a cada `/status` — e fica disponível em `GET /health`
(ver abaixo), porque o `print()` de log não escreve nada quando a API roda
destacada sob `pythonw.exe` (`sys.stdout is None`).

**Ausência é normal.** Sem o binário do herdr instalado, `herdr.start()` não
faz nada e não reclama: a API funciona exatamente como antes, com
`herdr_online: false` e nenhum órfão em `labels[]`.

### Os dois motores de estado

Duas fontes disputam o campo `state` de cada sessão. O **herdr** é a
principal: lê a tela renderizada, então não depende de hook nenhum — é ele
que resolve os dois furos estruturais da dedução por hooks ("desbloqueou" e
"interrompido" não têm hook). A **dedução por hooks** (`corrigir_estado_se_couber`
em `claude_metrics_api.py`) é a reserva: assume sozinha quando o herdr some ou
nunca esteve instalado, e continuar exercitada é o que evita que ela apodreça
em silêncio até o dia em que se precisa dela de novo. `server/motor.py`
escolhe entre as duas com histerese de 3 avaliações seguidas, para não
tremular numa piscada de 2s do herdr — e cai sozinho para o motor de hooks se
as avaliações pararem de acontecer (thread do sensor travada), em vez de
ficar grudado no último motor escolhido. O campo `motor` do `/status` e o
bloco `motor` do `/health` (abaixo) dizem qual dos dois está valendo; no
painel, a marca de rodapé só aparece quando é o motor de hooks — o caso
normal (herdr mandando) fica sem marca nenhuma.

### `POST /tela/pedir`, `POST /tela`, `GET /tela` — a foto da tela do painel

Corrigir o layout desta placa custava uma foto de celular por rodada: quem
desenha a tela não a enxerga. Estas três rotas trocam a foto pelo próprio
framebuffer.

**O pedido pega carona no polling.** A placa já pergunta `/status` a cada 2 s,
então não há rota de entrada nela — nada de servidor HTTP no firmware, nada de
porta aberta na LAN do lado da placa:

```
POST /tela/pedir  ──►  {"id": 1786720579, "ttl": 30}
                            │
                  GET /status traz  "captura": {"id": 1786720579}
                            ▼  (proximo poll, <=2s)
                  a placa copia o framebuffer e manda
POST /tela  (307.200 bytes, X-Tela: id=...; w=320; h=480; rot=1; fmt=raw)
                            ▼
                  tela.py grava  server/capturas/tela-<data>.png
GET /tela  ──►  {"estado": "pronto|pendente|expirado|vazio", "arquivo": ...}
```

O corpo é o framebuffer **cru**: 307.200 bytes fixos (320×480×2), RGB565
little-endian. Sem compressão de propósito — um RLE levaria a tela típica a
20–60 KB, e custaria um compressor no firmware, um módulo separado só para
testá-lo no PC, um caso de estouro em tela chapada e uma validação de expansão
aqui. Tamanho fixo faz a validação ser uma comparação de inteiros.

| Situação | Resposta |
|---|---|
| Corpo com tamanho diferente de `w*h*2` | `400` |
| `fmt` presente e diferente de `raw` | `400` — formato novo lido como cru viraria chuvisco, e chuvisco passa por defeito de firmware |
| `id` diferente do pedido pendente | `409` |
| Falha ao gravar (disco, permissão) | `500`, e `GET /tela` volta ao estado anterior |

**O `id` é semeado com `int(time.time())`.** O pedido vive em memória e esta API
reinicia a cada logon; um contador que recomeçasse do zero repetiria ids já
usados, e a placa descartaria a captura nova por achar que já a atendeu. Do lado
da placa a comparação é **por fonte e por desigualdade** — cada máquina tem seu
contador, e o mesmo número em duas delas são pedidos diferentes.

**O PNG é escrito à mão** (`zlib` + `struct`, `server/tela.py`), sem Pillow: a
mesma disciplina de `weather.py` e `works.py` — esta API sobe no logon e um
acessório dela não pode obrigar a máquina a um `pip install`. A gravação
acontece **fora da trava**: 300 KB de zlib segurando o `_lock` parariam o
`/status` que a placa pede a cada 2 s.

`rot` diz como a tela **está sendo olhada**, não como o quadro está na memória
(o framebuffer é sempre 320×480 nativo). Com `rot=1` o PNG sai 480×320.

As capturas ficam em `server/capturas/`, fora do git, e a própria gravação
mantém só as 20 mais recentes.

> **O que isto amplia.** Como todo o resto desta API, as rotas escutam em
> `0.0.0.0` sem autenticação. Quem estiver na LAN pode pedir uma foto da tela do
> painel — que mostra repo, branch e a pergunta que trava um agente. É o mesmo
> buraco de `/responder`, e a mesma resposta: fechar exige segredo compartilhado
> entre painel e API, que ainda não existe.

### `POST /arquivo/subir`, `POST /arquivo/pedir`, `GET /arquivo/baixar`, `POST /arquivo/ok`, `GET /arquivo` — arquivo novo no cartão

O caminho inverso da foto: pôr um `.clw` novo no microSD **sem tirar o cartão
da placa**. Antes disto, atualizar um sprite era desligar a placa, levar o
cartão ao PC e devolver — e o cartão mora atrás da tela.

```
tools/atualizar_sprite.py arquivo.clw
  ├── POST /arquivo/subir  (binário cru, header X-Arquivo: nome=...)
  │         o binário sobe PELA PRÓPRIA API, que o grava na staging dela
  └── POST /arquivo/pedir {"nome": ..., "reiniciar": true}
                ──►  {"id": N, "bytes": ..., "crc": ..., "ttl": 120}
                          │
                GET /status traz  "arquivo": {"id", "nome", "bytes", "crc", "reiniciar"}
                          ▼  (próximo poll, <=2s)
                a placa baixa  GET /arquivo/baixar  para a PSRAM,
                confere bytes e CRC-32, grava /clawd/<nome> (.tmp + rename)
POST /arquivo/ok {"id": N, "ok": true}   e reinicia (ESP.restart)
                          ▼
GET /arquivo  ──►  {"estado": "gravado|falhou|pendente|expirado|vazio", ...}
```

**A staging é da instância, não do repositório.** Ela é a pasta `atualizacoes/`
ao lado do `arquivo.py` que está rodando — que pode ser a cópia em
`~/.claude/metrics-api/`, e não a do repo. Por isso o disparador **sobe o
binário pela API** (`POST /arquivo/subir`) em vez de copiar arquivo no disco: a
primeira versão copiava para `server/atualizacoes/` do repo e levava `404` no
`pedir`. De quebra, isso passou a cobrir a máquina remota —
`tools/atualizar_sprite.py arquivo.clw --api http://192.168.0.11:8787` atualiza
o cartão da placa do PC2 sem nada tocar o disco dele.

`pedir` **congela** nome, tamanho e CRC no pedido: trocar o arquivo da staging
no meio do voo não muda o que a placa confere — o CRC falha, que é o
comportamento honesto. `/arquivo/baixar` serve **apenas** o arquivo do pedido
vivo; nome nenhum viaja na URL, então não há travessia de caminho a defender.
Do lado da placa o nome é conferido de novo no parse (sem `/`, `\` nem `..`) e
vira sempre `/clawd/<nome>`.

| Situação | Resposta |
|---|---|
| Nome com separador de caminho, vazio, oculto ou >63 chars | `400` no `subir` e no `pedir` |
| Arquivo não está na staging | `404` no `pedir` |
| Acima de 2 MB (teto que o firmware também impõe) | `400` no `subir` e no `pedir` |
| `GET /arquivo/baixar` sem pedido vivo | `404` |
| `ok` com id que não é o pendente | `409` |

Quem grava no cartão é o **laço** da placa, não a tarefa de rede: o SD é dele
(os sprites saem de lá no meio do desenho), e o FatFS não ganha dois escritores.
A gravação é `.tmp` + rename — queda de energia no meio não corrompe o sprite
que estava lá. O aviso (`/arquivo/ok`) sai **antes** do `ESP.restart()`, senão o
pedido renasceria no boot seguinte. Download que falha não é retentado: a placa
marca o id como visto, a API deixa o pedido expirar e `GET /arquivo` conta o
desfecho (`falhou`/`expirado`) a quem espera.

> **O que isto amplia, e amplia de verdade.** Com `POST /arquivo/subir`, quem
> estiver na LAN pode pôr um arquivo qualquer em `/clawd/` do cartão — não é
> mais preciso ter acesso ao disco do PC, que era a fronteira que a versão
> anterior cobrava. O que sobra de defesa é o nome (sem caminho, sem `..`, sem
> oculto, ≤63 chars, validado nos dois lados), o teto de 2 MB e o fato de o
> firmware só ler `/clawd/<nome>`. É o mesmo buraco sem autenticação das outras
> rotas, um degrau mais fundo.

### `GET /health` — liveness da própria API
```json
{
  "ok": true, "ttl": 900, "port": 8787,
  "herdr": {
    "binario": "C:\\Users\\voce\\AppData\\Local\\Programs\\Herdr\\bin\\herdr.exe",
    "online": true,
    "retrato_idade_s": 1.8
  },
  "divergencias": [
    { "sid": "2bfc2d48", "nosso": "working", "herdr": "idle", "pane": "w0:p1", "ts": 1785439521.4 }
  ],
  "motor": { "atual": "herdr", "desde": 1785439200.0, "trocas": 3 }
}
```

`herdr.binario` é `null` quando o sensor não está instalado. `divergencias` são
as últimas (até 20, `DIVERGENCIA_LOG_MAX`) vezes em que o herdr e os hooks
discordaram por tempo suficiente para virar notícia — ver [O sensor do
herdr](#o-sensor-do-herdr).

`motor` é o retrato de [`server/motor.py`](#os-dois-motores-de-estado):
`atual` (`"herdr"` ou `"hooks"`), `desde` (epoch da última troca, ou do boot
se nunca trocou) e `trocas` (quantas vezes já alternou desde que a API subiu).
Diagnóstico, não decisão — continua mostrando o motor **guardado** mesmo que
as avaliações tenham parado de chegar; quem decide de fato é o campo `motor`
de `/status`, que cai para `"hooks"` sozinho nesse caso.

### `GET /sessions` — debug: detalhe de cada sessão viva
Útil pra entender de onde veio cada número. Não use no firmware.

### `POST /ingest` — usado só pelo statusline.cjs
Body: `session_id`, `repo`, `branch`, `model`, `context_pct`, `session_pct`,
`session_resets_at`, `week_pct`, `week_resets_at` (os `*_at` são epoch absoluto),
`pid`, `herdr_pane_id`.

Traz **números**. Nunca sobrescreve o `state` que os hooks estabeleceram.
`herdr_pane_id`, quando presente, grava a mesma chave de pareamento que o hook
já grava — ver [O sensor do herdr](#o-sensor-do-herdr).

### `POST /event` — usado só pelo claude_hook.cjs
Body: `session_id`, `event`, `state`, `reason`, `kind`, `cwd`, `pid`,
`transcript_path`, `herdr_pane_id`.

Traz **estado**. `state: "closed"` remove a sessão imediatamente.

```powershell
# Simular um bloqueio e depois o encerramento:
$b = '{"session_id":"teste","event":"Notification","state":"blocked"}'
Invoke-RestMethod -Uri http://127.0.0.1:8787/event -Method Post -Body $b -ContentType 'application/json'
(Invoke-RestMethod http://127.0.0.1:8787/status).blocked   # -> 1

$b = '{"session_id":"teste","event":"SessionEnd","state":"closed"}'
Invoke-RestMethod -Uri http://127.0.0.1:8787/event -Method Post -Body $b -ContentType 'application/json'
# -> {"ok":true,"removed":true,...}
```

### `POST /responder` — o painel respondendo um prompt de aprovação

Body: `pane_id`, `n` (o número da opção), `rotulo` (o texto que estava escrito
nela). Responde `{"enviado": bool, ...}` — 200 quando o herdr aceitou, 409
quando a conferência abortou.

Não aceita texto nem tecla. Só o número de uma opção que o próprio agente está
oferecendo na tela dele, das que aparecem em `bloqueio.opcoes` do `/status`.

O caminho completo, em `bloqueio.responder`: relê a tela → acha a opção pelo
número → **confere o rótulo** → navega com `Up`/`Down` → relê e confere se o
cursor parou onde devia → só então `Enter`. Qualquer divergência aborta sem
mandar nada.

As duas conferências existem por motivos diferentes. O rótulo protege do tempo:
se a pergunta mudou entre o toque no painel e a chegada do POST, a resposta seria
dada à pergunta errada. A releitura depois de navegar protege do espaço: se o
cursor não andou (agente ocupado, tela redesenhada no meio), o `Enter` aprovaria
a opção errada — que é o pior desfecho possível desta rota.

> **O que isto amplia.** A API escuta em `0.0.0.0` sem autenticação, e essa
> decisão foi tomada quando o pior que alguém na LAN conseguia era compactar uma
> sessão (ver a lista branca `COMANDOS`). Com esta rota, o pior vira **aprovar o
> que o agente estiver perguntando agora**. As conferências acima impedem
> resposta às cegas e resposta atrasada; não impedem quem leia o `/status`, que é
> aberto, e responda de propósito. Fechar isso exige segredo compartilhado entre
> painel e API — ainda não existe.

```powershell
# O que está travando alguém agora:
(Invoke-RestMethod http://127.0.0.1:8787/status).bloqueio

# Responder a opção 1 (o rótulo tem que bater com o da tela):
$b = '{"pane_id":"w0:p1","n":1,"rotulo":"Yes"}'
Invoke-RestMethod -Uri http://127.0.0.1:8787/responder -Method Post -Body $b -ContentType 'application/json'
```

## Notas para o firmware ESP32-S3

- Poll: o firmware deste repo usa **2s**. O TTL é 30s, então há folga larga, e
  numa rede local o custo de uma requisição é irrisório.
- `HTTPClient` + `ArduinoJson` bastam. **Atenção:** o payload real mede ~643
  bytes com `labels` e `colors` — um buffer de 512 bytes **não** serve. Use o
  `JsonDocument` elástico do ArduinoJson 7 e não dimensione na mão.
- Trate `online: false` como estado "Claude offline" na tela.
- Se o GET falhar (API fora do ar / PC desligado), mantenha o último estado
  esmaecido com a idade explícita — nunca trave nem apague o dado.
- Não reimplemente as faixas de cor: `colors.*` já vem calculado. E respeite
  `limits_fresh: false` mostrando `—`, nunca um número.

O firmware que consome esta API está em `../src` deste mesmo repositório.

## Firewall — resolvido, mas não pelo motivo esperado

**Verificado em 30/07/2026 com o ESP32-S3 real: a placa lê `/status` sem
nenhuma regra adicional.**

O aviso anterior deste README estava certo nos fatos e errado na conclusão. Os
fatos continuam valendo: a regra por porta `Claude Metrics API 8787` cobre só
**Private+Domain**, e a Wi-Fi deste PC está como **Public** — logo, essa regra
de fato **não se aplica**.

O que faltava perceber é que o Firewall do Windows autoriza por porta **ou** por
programa, e existe uma regra por aplicativo liberando o `pythonw.exe` nos perfis
**Private e Public**, criada quando a API subiu pela primeira vez. É ela que
permite a conexão.

```powershell
# Conferir a regra por aplicativo (a que realmente vale aqui):
Get-NetFirewallApplicationFilter | Where-Object { $_.Program -like '*python*' } |
    ForEach-Object { $r = $_ | Get-NetFirewallRule
        if ($r.Enabled -eq 'True' -and $r.Direction -eq 'Inbound') {
            "{0} | perfil={1}" -f $r.DisplayName, $r.Profile } }
```

**Lição de diagnóstico:** olhar só `Get-NetFirewallPortFilter` leva à conclusão
errada de que a porta está bloqueada. Cheque sempre as duas formas.

Se um dia a regra do `pythonw.exe` sumir, ou a API passar a rodar por outro
executável, aí as saídas antigas voltam a valer: marcar a Wi-Fi como **Privada**
(preferível — mantém a porta fechada em redes públicas) ou
`Set-NetFirewallRule -DisplayName "Claude Metrics API 8787" -Profile Any`.

Teste a partir de outro device na rede: `http://192.168.0.10:8787/status`

## Instalação do zero

São quatro peças. Sem qualquer uma delas a corrente quebra em silêncio: a
statusline continua funcionando, a API responde `online: false`, e a placa
mostra "CLAUDIO OFFLINE" sem dizer o porquê.

**1. Publisher** — copie `statusline.cjs` para `~/.claude/` e registre em
`~/.claude/settings.json`:

```json
"statusLine": {
  "type": "command",
  "command": "node \"C:\\Users\\<voce>\\.claude\\statusline.cjs\"",
  "refreshInterval": 10
}
```

O `refreshInterval: 10` **é o heartbeat do sistema**. Publicar a cada 10s dá três
chances antes dos 30s de `FRESH_SECONDS`. Aumentar muito esse valor faz sessões
vivas parecerem paradas.

**2. Hooks** — copie `claude_hook.cjs` para `~/.claude/` e registre os cinco
eventos no mesmo `settings.json`. Um script só atende todos: ele lê
`hook_event_name` do stdin e decide o estado.

```json
"hooks": {
  "SessionStart":     [{ "hooks": [{ "type": "command", "command": "node \"C:\\Users\\<voce>\\.claude\\claude_hook.cjs\"", "timeout": 5 }] }],
  "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "node \"C:\\Users\\<voce>\\.claude\\claude_hook.cjs\"", "timeout": 5 }] }],
  "Stop":             [{ "hooks": [{ "type": "command", "command": "node \"C:\\Users\\<voce>\\.claude\\claude_hook.cjs\"", "timeout": 5 }] }],
  "Notification":     [{ "matcher": "permission_prompt|agent_needs_input|idle_prompt|elicitation_dialog",
                         "hooks": [{ "type": "command", "command": "node \"C:\\Users\\<voce>\\.claude\\claude_hook.cjs\"", "timeout": 5 }] }],
  "SessionEnd":       [{ "hooks": [{ "type": "command", "command": "node \"C:\\Users\\<voce>\\.claude\\claude_hook.cjs\"", "timeout": 5 }] }]
}
```

O `matcher` do `Notification` importa. `auth_success` e `agent_completed` também
são notificações, mas **não** bloqueiam nada.

**`idle_prompt` também fica de fora, e essa foi uma lição cara.** A primeira
versão o incluía, pelo raciocínio "deixou o agente esperando = precisa de você".
Errado: `idle_prompt` dispara quando a sessão passa um tempo **sem você
digitar** — ou seja, quando ela está *ociosa*, que é exatamente o oposto de
bloqueada. Na prática, toda sessão que você deixava quieta virava `blocked`, e o
painel colapsava justamente a distinção que este projeto existe para fazer.

> **Sessões já abertas pegam os hooks sem reiniciar** — verificado em 30/07/2026:
> uma sessão em curso passou a `hooked: true` no primeiro `UserPromptSubmit`
> depois da edição. Mas ela só reporta no **próximo evento**, então continua
> `state: "unknown"` (que, de propósito, não pulsa) até você interagir com ela.

Um hook roda de forma **síncrona** — o Claude Code espera por ele. Por isso o
script é fire-and-forget com prazo de 400ms e engole todo erro: API fora do ar
não pode travar o seu terminal.

**3. API** — copie `claude_metrics_api.py` para uma pasta fixa e rode
`setup-elevado.ps1` num PowerShell **como Administrador**. Ele cria a regra de
firewall e a tarefa agendada que sobe a API no logon. Ajuste os caminhos
absolutos dentro do script antes de rodar.

**4. Placa** — grave o firmware deste repositório e ponha um `config.json` na
raiz do cartão SD apontando para o IP desta máquina:

```json
{
  "wifi": { "ssid": "SUA_REDE", "password": "SUA_SENHA" },
  "api":  { "url": "http://192.168.0.10:8787/status" },
  "poll_ms": 2000,
  "brightness": 200
}
```

O IP precisa ser estável — reserve-o por DHCP no roteador. Se ele mudar, a placa
mostra `API FORA` e você vai procurar defeito no lugar errado.

### Conferir a corrente, peça por peça

```powershell
# 1. A API esta viva?
Invoke-WebRequest http://127.0.0.1:8787/health -UseBasicParsing

# 2. O publisher esta alimentando? (sessions > 0)
(Invoke-WebRequest http://127.0.0.1:8787/status -UseBasicParsing).Content

# 3. Os hooks estao instalados? `hooked` precisa ser True em sessao reiniciada.
(Invoke-RestMethod http://127.0.0.1:8787/sessions).sessions |
    Select-Object repo, state, event, hooked, age | Format-Table

# 4. A rede deixa passar? Use o IP, NAO localhost — e o caminho que a placa usa.
Invoke-WebRequest http://192.168.0.10:8787/health -UseBasicParsing
```

O passo 4 é o que costuma falhar, e testá-lo por `localhost` esconde o problema.
No passo 3, `hooked: False` em toda sessão significa que o `settings.json` não
foi lido — reinicie uma sessão e confira de novo.

## Operação

- **Auto-start:** tarefa agendada `ClaudeMetricsAPI` roda no logon via `pythonw.exe` (sem janela).
- **Subir na mão:**
  ```powershell
  Start-Process pythonw "$env:USERPROFILE\.claude\metrics-api\claude_metrics_api.py" -WindowStyle Hidden
  ```
- **Parar:** `Get-Process pythonw | Stop-Process` (cuidado: mata outros scripts pythonw)
- **Ver se está no ar:** abra `http://127.0.0.1:8787/status` no navegador.

## Arquivos

> **Onde isto roda.** A cópia viva fica em `%USERPROFILE%\.claude\metrics-api\`
> e é a que a tarefa agendada executa no logon. Esta pasta do repositório é a
> versionada — ao editar aqui, copie de volta para lá (e vice-versa), ou as duas
> divergem em silêncio. Em 30/07/2026 elas estão idênticas.

| Arquivo | Papel |
|---------|-------|
| `claude_metrics_api.py` | A API (stdlib pura, zero dependências) |
| `herdr.py` | Sensor do herdr: consulta a CLI numa thread de fundo — ver [O sensor do herdr](#o-sensor-do-herdr) |
| `tela.py` | Framebuffer cru virando PNG: valida o tamanho, replica os bits de RGB565 para 888, gira conforme `rot` e mantém só as 20 últimas capturas |
| `weather.py` | Data, hora e tempo. Thread própria, cache de 10 min, nunca levanta exceção |
| `weather.example.json` | Modelo das coordenadas, para quando a geolocalização por IP errar a cidade |
| `weather.json` (ao lado dela) | Coordenadas em uso. Gerado no primeiro boot, **não** versionado |
| `tray.pyw` | Ícone de bandeja: status, avisos e controle da API |
| `api.pid` (ao lado dela) | PID da API, para o tray poder parar/reiniciar. Runtime, **não** versionado |
| `state.json` (ao lado dela) | Estado das sessões, para sobreviver a reinício. Gerado em runtime, **não** versionado |
| `claude_hook.cjs` → `~/.claude/` | Hooks de ciclo de vida: publica o **estado** |
| `statusline.cjs` → `~/.claude/` | Statusline + publisher: publica os **números** |
| `~/.claude/settings.json` | `statusLine.refreshInterval: 10` + os 5 hooks |
| `setup-elevado.ps1` | Script que criou firewall + tarefa agendada |
| `~/.claude/statusline.cjs.bak` | Backup da statusline anterior |

## `tray.pyw` — ícone na bandeja

```powershell
pythonw server\tray.pyw
```

A cor do ícone diz o estado agregado, na mesma linguagem visual do painel:

| Cor | Significa |
|-----|-----------|
| 🔴 vermelho | algum agente **bloqueado** |
| 🟠 laranja | algum trabalhando |
| 🟢 verde | tudo ocioso |
| ⚪ cinza vazado | API fora do ar |

**Clique esquerdo** abre `/status`. **Clique direito** abre o menu: lista dos
agentes com estado e contexto, os limites de 5h e 7 dias, e as ações de abrir /
reiniciar / parar / iniciar a API.

**Balão de aviso** quando um agente ENTRA em `blocked` — só na transição, nunca
repetido. Avisar a cada poll viraria ruído, e ruído constante é como um alerta
deixa de ser lido.

### Duas decisões

**Zero dependências**, via Win32 puro com `ctypes`. Uma biblioteca de tray
resolveria em 20 linhas o que aqui custa ~200 — mas a API já é stdlib pura e
roda no logon, e um acessório dela não pode ser o motivo de a máquina precisar
de `pip install`. O preço é pago uma vez, no código, e nunca em instalação.

**Sem threads.** A busca roda no `WM_TIMER`, na própria fila de mensagens:
contra `127.0.0.1` ela custa 2 a 14 ms medidos, então não trava a interface, e
some toda a classe de bug de estado compartilhado entre thread e GUI.

### A armadilha do ctypes, que custou duas rodadas

**Declare `argtypes` e `restype` de toda função que recebe ou devolve handle.**
Sem isso o ctypes infere o tipo a partir do valor Python, e um handle de 64 bits
estoura com `OverflowError: int too long to convert`.

O pior não é o erro, é o silêncio: onde o valor cabe por acaso, funciona. Este
arquivo rodou inteiro sob `python.exe` e morreu no `CreateWindowExW` sob
`pythonw.exe`, só porque o módulo carregou em outro endereço. E dentro do
callback de janela o erro vira `Exception ignored` — o programa segue rodando
meio quebrado em vez de morrer.

### Subir no logon

```powershell
$pyw = Join-Path (Split-Path (Get-Command python).Source) 'pythonw.exe'
$lnk = Join-Path ([Environment]::GetFolderPath('Startup')) 'Claude Metrics Tray.lnk'
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$s.TargetPath = $pyw; $s.Arguments = '"<caminho>\tray.pyw"'; $s.Save()
```

Para desfazer, apague o atalho: `explorer shell:startup`.

## Design: por que assim

- **stdlib, sem FastAPI/Flask:** servir 200 bytes de JSON não justifica dependência nem `pip install`.
- **Memória + `state.json`, sem banco:** os números repovoam sozinhos em 10s pelo heartbeat, então
  não precisariam de disco. O **estado** precisa: uma sessão bloqueada não consegue se reanunciar —
  parou de publicar justamente porque está travada. Sem persistência ela sumia do painel no reinício
  da API e só voltava quando você mexia nela, ou seja, quando o aviso já não servia.
- **Quem remove, grava.** O `save_state()` mora **dentro** do `prune()`, e não em quem o chama.
  A primeira versão salvava só no `/ingest` e no `/event` — justamente os dois caminhos que rodam
  menos. A poda que acontecia durante um `GET /status` apagava a sessão da memória e a deixava no
  `state.json`, e ela **voltava do túmulo** no reinício seguinte. Sintoma: "fechei a sessão e ela
  continua aparecendo".
- **Agregação na leitura:** os publishers ficam burros e rápidos; a lógica mora num lugar só.
- **Publishers à prova de falha:** o `stdout.write` acontece **antes** do POST; erro é engolido em
  silêncio com timeout curto. API morta = statusline e hooks funcionam igual, você nem percebe.
- **Hooks mandam, PID é coadjuvante.** Vale repetir porque a intuição diz o contrário: o PID parece
  a fonte de verdade óbvia e **não é** (ver "Por que `pid` não decide nada").
