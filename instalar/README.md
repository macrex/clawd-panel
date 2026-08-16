# Instalação do painel Clawd — master e slave

O painel Clawd é uma placa ESP32-S3 (Guition JC3248W535, tela 3,5") que mostra o uso do
Claude Code: sessões vivas, limites de 5 h/7 dias, perguntas de agentes bloqueados. Ela
consome `GET /status` de um **servidor de métricas** (Python, porta `8787`) que roda em
cada PC onde há Claude Code.

Com **duas máquinas**, os papéis são:

| papel | o que roda | o que aparece no painel |
|---|---|---|
| **master** | servidor de métricas + hooks + statusline + **a placa aponta para ele** | tudo: cabeçalho, relógio, clima, limites, agentes locais |
| **slave** | servidor de métricas + hooks + statusline (instalação padrão, idêntica) | só os agentes dele, com a tag da máquina em violeta; vira reserva do cabeçalho se o master cair |

Cada máquina se identifica com uma **tag de 3 caracteres** que aparece no primeiro chip
de cada agente — a do master em verde, a da segunda em violeta. Com uma máquina só,
nenhuma tag é desenhada.

Quem define a tag é o **serviço**, não o cartão da placa: a variável de ambiente
`CLAWD_TAG` na máquina (sem ela, as três primeiras letras alfanuméricas do hostname).
É por isso que instalar uma terceira máquina não pede abrir a placa — ela se apresenta
sozinha no `/status`.

Quem decide o que usar de cada máquina é **a placa** (firmware): o servidor do slave não
sabe que é slave — é uma instalação normal. A diferença inteira mora no `config.json` do
microSD do master (`api.url` = master, `api.url2` = slave).

## Qual arquivo seguir

- **[SLAVE.md](SLAVE.md)** — configurar uma máquina nova como slave. É o runbook para o
  pedido *"configura essa máquina para ser um slave para o meu esp32"*.
- **[MASTER.md](MASTER.md)** — montar um master do zero (servidor + placa + microSD +
  firmware) ou registrar um slave num master existente.

## Para agentes (Claude Code, etc.)

Se o usuário pediu algo como **"configura essa máquina para ser um slave para o meu
esp32"** / "instala o modo slave" / "quero que este PC mande os agentes pro painel":
abra `instalar/SLAVE.md` e execute os passos na ordem, sem pular as verificações.
Se o pedido é sobre a placa, o microSD, o firmware ou "ser o master": `instalar/MASTER.md`.

## Requisitos comuns (as duas máquinas)

- **Windows 10/11 com PowerShell** ou **macOS** (zsh + `launchd`). Os runbooks trazem os
  dois caminhos passo a passo; misturar master Windows com slave macOS é suportado — a
  placa só vê `/status` por HTTP, não sabe de sistema operacional.
- Claude Code instalado e usado nesta máquina (é ele quem gera as métricas).
- Node.js no PATH (`node --version`) — statusline e hooks são `.cjs`.
- Python 3.10+ (`python --version` / `python3 --version` no macOS). O servidor é
  **stdlib pura**: não há `pip install`.
- As duas máquinas e a placa na **mesma LAN**.

> **A placa em si só é gravada de um PC com PlatformIO** (seção C do MASTER.md) — no
> macOS a build funciona, muda o caminho do venv e o nome da porta serial. O slave não
> encosta em hardware nenhum.

Documentação profunda do servidor (endpoints, motores herdr/hooks, firewall, formato dos
payloads): [`server/README.md`](../server/README.md).
