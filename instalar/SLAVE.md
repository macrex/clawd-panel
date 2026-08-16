# Configurar esta máquina como SLAVE do painel Clawd

> **Runbook para agente.** Gatilho típico: *"configura essa máquina para ser um slave
> para o meu esp32"*. Execute os passos na ordem. Cada passo tem uma **verificação** —
> não avance com verificação falhando. Ao final, produza o relatório do passo 10.
>
> **Duas plataformas.** Cada passo traz o bloco **Windows (PowerShell)** e o bloco
> **macOS (zsh)**. Escolha um e siga só ele até o fim — não misture. O servidor é o
> mesmo código Python nos dois; o que muda é onde a variável de ambiente mora e quem
> sobe o processo no logon (Startup no Windows, `launchd` no macOS).
>
> **Cada bloco é autocontido de propósito.** Variáveis de shell não sobrevivem entre
> invocações em muitos ambientes de agente: por isso os caminhos são repetidos em vez de
> guardados numa variável do passo anterior. Não "melhore" isso extraindo variáveis
> compartilhadas — o runbook quebra quando os blocos rodam separados.

**O que você vai montar:** o servidor de métricas (Python, porta `8787`) + statusline +
hooks do Claude Code desta máquina. Com isso, os agentes daqui passam a ser publicados
em `http://<IP-desta-máquina>:8787/status`, que a placa do usuário consome.

**O que você NÃO precisa:** placa, microSD, PlatformIO, firmware, flash. Nada de
hardware acontece no slave. Também não há modo/flag "slave" no servidor — a instalação
é a padrão; quem o trata como slave é a placa.

---

## 0. Pré-checagens

**Windows (PowerShell)**

```powershell
node --version        # precisa existir (v18+ serve)
python --version      # 3.10 ou superior
claude --version      # Claude Code instalado
```

- Sem Python: `winget install -e --id Python.Python.3.12` e reabra o terminal.
- Sem Node: `winget install -e --id OpenJS.NodeJS.LTS` e reabra o terminal.
- `python` não encontrado mas `py` sim (launcher do Windows): use `py` no lugar de
  `python` nos comandos, e confirme que `pythonw.exe` existe
  (`(Get-Command pythonw).Source`) — o passo 6 depende dele para rodar sem janela.
  Ausente (instalação pela Microsoft Store), instale o Python pelo winget acima.

**macOS (zsh)**

```bash
node --version        # precisa existir (v18+ serve)
python3 --version     # 3.10 ou superior
claude --version      # Claude Code instalado
```

- Sem Python: `brew install python` e reabra o terminal.
- Sem Node: `brew install node` e reabra o terminal.
- Use **`python3`** em todos os comandos: o `python` sem sufixo pode não existir, ou
  ser o Python 2 do sistema em versões antigas do macOS.
- Não existe `pythonw` no macOS, e não faz falta: o `launchd` (passo 6) roda o processo
  sem terminal e sem janela.
- **Apple Silicon:** o Homebrew instala em `/opt/homebrew/bin`, não em `/usr/local/bin`.
  Anote a saída de `command -v python3` — o passo 6 precisa do caminho absoluto real.

**Sem Claude Code (as duas):** pare e avise o usuário — sem ele não há métrica nenhuma
para publicar.

Porta livre?

```powershell
Get-NetTCPConnection -LocalPort 8787 -State Listen -ErrorAction SilentlyContinue
```

```bash
lsof -nP -iTCP:8787 -sTCP:LISTEN
```

Saída vazia = livre. Se algo já escuta 8787, veja Troubleshooting antes de seguir.

## 1. Obter o software

Você precisa da pasta `server/` do projeto nesta máquina. O runbook assume que ela ficou
em `%USERPROFILE%\esp32-s3\server\` (Windows) ou `~/esp32-s3/server/` (macOS) — use esse
caminho ou ajuste todos os blocos abaixo. Um clone que já exista em outro lugar serve:
troque o caminho, não mova a pasta.

**Opção A — clone (se o repositório já publica o modo dois PCs):**

```powershell
git clone https://github.com/macrex/clawd-panel.git "$env:USERPROFILE\esp32-s3"
```

```bash
git clone https://github.com/macrex/clawd-panel.git ~/esp32-s3
```

**Opção B — cópia manual** (pendrive, pasta de rede, `scp`): copie a pasta `server/` do
projeto para o caminho acima. Basta ela; o firmware não é usado aqui.

**Verificação obrigatória — a versão importa:**

```powershell
Select-String -Path "$env:USERPROFILE\esp32-s3\server\claude_metrics_api.py" -Pattern "def tag_da_maquina" -Quiet
```

```bash
grep -q "def tag_da_maquina" ~/esp32-s3/server/claude_metrics_api.py && echo True || echo False
```

`True` = a cópia publica o campo `tag` e esta máquina vai se identificar no painel.
`False` = versão anterior ao modo dois PCs: **pare**. O servidor até funciona, mas os
agentes daqui aparecem sem tag no painel, indistinguíveis dos da outra máquina. Peça ao
usuário uma cópia atual de `server/`.

## 2. Instalar o servidor de métricas

O servidor roda de `~\.claude\metrics-api\` (`~/.claude/metrics-api/` no macOS), uma
cópia dos módulos de `server/`. Os `test_*.py`, `statusline.cjs`, `claude_hook.cjs` e
`setup-elevado.ps1` **não** vão junto.

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.claude\metrics-api" | Out-Null
@("claude_metrics_api.py","bloqueio.py","herdr.py","motor.py","precos.py",
  "terminal.py","texto.py","transcript.py","weather.py","works.py",
  "tray.pyw","weather.example.json") | ForEach-Object {
    Copy-Item "$env:USERPROFILE\esp32-s3\server\$_" "$env:USERPROFILE\.claude\metrics-api" -Force
}
```

```bash
mkdir -p ~/.claude/metrics-api
for f in claude_metrics_api.py bloqueio.py herdr.py motor.py precos.py \
         terminal.py texto.py transcript.py weather.py works.py \
         tray.pyw weather.example.json; do
  cp -f ~/esp32-s3/server/"$f" ~/.claude/metrics-api/
done
```

São exatamente os 12 arquivos que o servidor importa, direta ou indiretamente
(`bloqueio`→`herdr`+`texto`, `works`→`precos`+`transcript`, `terminal`→`texto`). Faltar
um deles derruba o processo no import, sem janela para mostrar o erro. (`tray.pyw` é da
bandeja do Windows e não roda no macOS — copie assim mesmo: a lista é a mesma nos dois,
e um arquivo inerte é mais barato que duas listas para manter em sincronia.)

**Verificação** (checa o que FALTA, e não o total: numa reinstalação a pasta também tem
`state.json`, `works.jsonl`, `weather.json` e `api.pid`, criados em runtime):

```powershell
@("claude_metrics_api.py","bloqueio.py","herdr.py","motor.py","precos.py",
  "terminal.py","texto.py","transcript.py","weather.py","works.py",
  "tray.pyw","weather.example.json") | Where-Object {
    -not (Test-Path "$env:USERPROFILE\.claude\metrics-api\$_")
}
```

```bash
for f in claude_metrics_api.py bloqueio.py herdr.py motor.py precos.py \
         terminal.py texto.py transcript.py weather.py works.py \
         tray.pyw weather.example.json; do
  [ -f ~/.claude/metrics-api/"$f" ] || echo "FALTA: $f"
done
```

Saída vazia = os 12 estão lá. Qualquer nome impresso é um arquivo que não foi copiado.

> Reinstalação/atualização: os mesmos comandos, por cima. `state.json` e `works.jsonl`
> (criados em runtime) não são tocados — são o estado e o livro-caixa DESTA máquina,
> nunca copie os do master.

## 3. Instalar a statusline

A statusline imprime a linha de status do Claude Code **e** publica as métricas da
sessão em `POST http://127.0.0.1:8787/ingest` a cada refresh.

```powershell
# Backup se já houver uma statusline própria nesta máquina
if (Test-Path "$env:USERPROFILE\.claude\statusline.cjs") {
  Copy-Item "$env:USERPROFILE\.claude\statusline.cjs" "$env:USERPROFILE\.claude\statusline.cjs.bak" -Force
}
Copy-Item "$env:USERPROFILE\esp32-s3\server\statusline.cjs" "$env:USERPROFILE\.claude\statusline.cjs" -Force
Copy-Item "$env:USERPROFILE\esp32-s3\server\claude_hook.cjs" "$env:USERPROFILE\.claude\claude_hook.cjs" -Force
```

```bash
# Backup se já houver uma statusline própria nesta máquina
[ -f ~/.claude/statusline.cjs ] && cp -f ~/.claude/statusline.cjs ~/.claude/statusline.cjs.bak
cp -f ~/esp32-s3/server/statusline.cjs ~/.claude/statusline.cjs
cp -f ~/esp32-s3/server/claude_hook.cjs ~/.claude/claude_hook.cjs
```

Se existia um `statusline.cjs.bak`, anote no relatório final: a linha visual antiga foi
substituída pela do projeto (se o usuário quiser a antiga de volta, é preciso fundir as
duas à mão — a do projeto aceita conviver, ela só ADICIONA o `POST /ingest`).

## 4. Registrar statusline e hooks no `settings.json`

O arquivo é `~/.claude/settings.json` nas duas plataformas. **Merge, nunca
sobrescrever**: a máquina pode ter hooks de outras ferramentas (`PreToolUse`,
`PostToolUse`, um `SessionStart` alheio) — eles ficam. Salve o script abaixo em um
arquivo temporário e rode uma vez; ele é idempotente (rodar de novo não duplica nada) e
faz backup em `settings.json.bak-esp32`.

O script é o mesmo nos dois sistemas: `os.path.expanduser("~")` resolve o home de cada um.

```python
import json, os, shutil

caminho = os.path.expanduser("~/.claude/settings.json")
usuario = os.path.expanduser("~").replace("\\", "/")
no = f'node "{usuario}/.claude/claude_hook.cjs"'
sl = f'node "{usuario}/.claude/statusline.cjs"'

cfg = {}
if os.path.exists(caminho):
    shutil.copy2(caminho, caminho + ".bak-esp32")
    with open(caminho, encoding="utf-8") as f:
        cfg = json.load(f)

# statusline do projeto assume SEMPRE (o backup do passo 3 preserva a anterior)
cfg["statusLine"] = {"type": "command", "command": sl, "refreshInterval": 10}

hooks = cfg.setdefault("hooks", {})
def anexar(evento, matcher=None):
    lista = hooks.setdefault(evento, [])
    for entrada in lista:                       # idempotencia: ja instalado?
        for h in entrada.get("hooks", []):
            if "claude_hook.cjs" in h.get("command", ""):
                return
    nova = {"hooks": [{"type": "command", "command": no, "timeout": 5}]}
    if matcher:
        nova["matcher"] = matcher
    lista.append(nova)

anexar("SessionStart")
anexar("UserPromptSubmit")
anexar("Stop")
anexar("SessionEnd")
anexar("Notification", "permission_prompt|agent_needs_input|elicitation_dialog")

with open(caminho, "w", encoding="utf-8") as f:
    json.dump(cfg, f, ensure_ascii=False, indent=2)
print("ok: statusline + hooks registrados; backup em settings.json.bak-esp32")
```

```powershell
python "$env:TEMP\instalar_hooks_clawd.py"
```

```bash
python3 /tmp/instalar_hooks_clawd.py
```

**Verificação:**

```powershell
python -c "import json,os;d=json.load(open(os.path.expanduser('~/.claude/settings.json'),encoding='utf-8'));print(d['statusLine']['command']);print(sorted(k for k,v in d['hooks'].items() if 'claude_hook' in json.dumps(v)))"
```

```bash
python3 -c "import json,os;d=json.load(open(os.path.expanduser('~/.claude/settings.json'),encoding='utf-8'));print(d['statusLine']['command']);print(sorted(k for k,v in d['hooks'].items() if 'claude_hook' in json.dumps(v)))"
```

Deve imprimir o caminho da statusline e
`['Notification', 'SessionEnd', 'SessionStart', 'Stop', 'UserPromptSubmit']`.

## 5. A tag desta máquina

Três caracteres que identificam esta máquina no painel — é o chip que aparece em cada
agente daqui quando há mais de uma fonte. Pergunte ao usuário qual quer; sem resposta,
proponha as três primeiras letras do hostname.

Sem a variável o servidor usa as três primeiras letras alfanuméricas do hostname
(`NOTE-CASA` → `NOT`). Mais que 3 caracteres é cortado; pontuação é descartada. Nada
disso vai para o cartão da placa: quem se identifica é o serviço.

**Windows (PowerShell)** — troque `PC2` pela tag escolhida:

```powershell
[Environment]::SetEnvironmentVariable("CLAWD_TAG","PC2","User")
$env:CLAWD_TAG = "PC2"      # o passo 6 sobe o servidor com ela no ambiente
```

**macOS (zsh)** — troque `MAC` pela tag escolhida. O macOS não tem escopo "User" de
variável de ambiente, então a tag mora em **três** lugares, cada um com um público:

```bash
# 1. terminais novos (e um servidor iniciado à mão)
printf '\n# Clawd (painel ESP32) — tag desta maquina\nexport CLAWD_TAG="MAC"\n' >> ~/.zshrc
# 2. sessão gráfica atual (apps abertos daqui em diante)
launchctl setenv CLAWD_TAG MAC
# 3. o servidor em si — dentro do plist do passo 6, que é quem manda de verdade
```

O item 3 não é redundância: agentes `launchd` **não** leem o `~/.zshrc` nem herdam o
ambiente do seu terminal. Sem `CLAWD_TAG` no plist, o servidor volta com a tag do
hostname no próximo reboot, mesmo com os itens 1 e 2 no lugar.

**Verificação (macOS):**

```bash
grep CLAWD_TAG ~/.zshrc && launchctl getenv CLAWD_TAG
```

## 6. Subir o servidor agora + auto-start no logon

O servidor escuta em `0.0.0.0:8787` (precisa: a placa chega pela LAN).

### Windows

`pythonw.exe` roda sem janela. O bloco abaixo **também define `CLAWD_TAG` no processo** —
sem isso o servidor sobe com a tag do hostname, porque a variável de escopo User só
alcança processos criados depois do próximo logon. Troque `PC2` pela tag do passo 5.

```powershell
$env:CLAWD_TAG = "PC2"
Start-Process (Get-Command pythonw).Source -ArgumentList "`"$env:USERPROFILE\.claude\metrics-api\claude_metrics_api.py`"" -WorkingDirectory "$env:USERPROFILE\.claude\metrics-api"
```

Auto-start (atalho na pasta Startup — replica a instalação de referência). O atalho não
precisa da variável: no logon seguinte o Windows já entrega a do escopo User.

```powershell
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut("$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup\Claude Metrics API.lnk")
$lnk.TargetPath = (Get-Command pythonw).Source
$lnk.Arguments = "`"$env:USERPROFILE\.claude\metrics-api\claude_metrics_api.py`""
$lnk.WorkingDirectory = "$env:USERPROFILE\.claude\metrics-api"
$lnk.Save()
```

### macOS

Um `LaunchAgent` resolve as duas coisas de uma vez — sobe agora e no logon, com a tag no
ambiente do processo. Crie `~/Library/LaunchAgents/com.clawd.metrics-api.plist`,
trocando `MAC` pela tag do passo 5, `/usr/local/bin/python3` pela saída de
`command -v python3` (Apple Silicon: `/opt/homebrew/bin/python3`) e
`seu-usuario` pelo seu usuário — **o plist não expande `~` nem variáveis**, todos os
caminhos são absolutos:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.clawd.metrics-api</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/python3</string>
        <string>/Users/SEU_USUARIO/.claude/metrics-api/claude_metrics_api.py</string>
    </array>
    <key>WorkingDirectory</key>
    <string>/Users/SEU_USUARIO/.claude/metrics-api</string>
    <key>EnvironmentVariables</key>
    <dict>
        <key>CLAWD_TAG</key>
        <string>MAC</string>
    </dict>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/Users/SEU_USUARIO/.claude/metrics-api/api.log</string>
    <key>StandardErrorPath</key>
    <string>/Users/SEU_USUARIO/.claude/metrics-api/api.err.log</string>
</dict>
</plist>
```

`KeepAlive` religa o servidor se ele morrer — é o equivalente prático da bandeja do
Windows. `StandardErrorPath` é o que substitui a janela de terminal que você não tem:
todo traceback de import cai em `api.err.log`.

Carregue (o `bootout` antes é o que torna o bloco repetível — sem ele, recarregar dá
`service already loaded`):

```bash
launchctl bootout gui/$(id -u)/com.clawd.metrics-api 2>/dev/null
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.clawd.metrics-api.plist
launchctl print gui/$(id -u)/com.clawd.metrics-api | grep -E "state =|pid ="
```

**Verificação (as duas plataformas):**

```powershell
$h = Invoke-RestMethod http://127.0.0.1:8787/health
"ok=$($h.ok) port=$($h.port) tag=$($h.tag)"
```

```bash
curl -s http://127.0.0.1:8787/health | python3 -c "import sys,json;h=json.load(sys.stdin);print('ok=%s port=%s tag=%s' % (h.get('ok'),h.get('port'),h.get('tag')))"
```

Deve responder `ok=True port=8787` e a `tag` que você definiu no passo 5. Tag diferente
da escolhida = o servidor subiu sem a variável no processo: repita o bloco acima
(no macOS, confira o `EnvironmentVariables` do plist e recarregue com `bootout` +
`bootstrap`). (`herdr.online: false` é normal — ver passo 8.)

## 7. Liberar a porta no firewall

Sem isto o `/health` local funciona mas **a placa não alcança** o servidor.

### Windows (precisa de admin)

```powershell
Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-Command',
  'New-NetFirewallRule -DisplayName "Claude Metrics API 8787" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8787'
```

**Verificação** (depois de aceitar o UAC):

```powershell
Get-NetFirewallRule -DisplayName "Claude Metrics API 8787" | Select-Object Enabled, Action
```

Se o usuário recusar a elevação, registre a pendência no relatório — tudo o mais
funciona, só o acesso da placa fica bloqueado até criar a regra.

### macOS

O firewall do macOS é **por aplicativo, não por porta** — não existe regra "libere a
8787". Comece perguntando se ele está sequer ligado:

```bash
/usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate
```

`Firewall is disabled. (State = 0)` = **nada a fazer**, a porta já está acessível pela
LAN; anote no relatório e siga. Ligado (State = 1 ou 2), autorize o binário do Python
que o plist usa (precisa de `sudo`; use o caminho real de `command -v python3`):

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /usr/local/bin/python3
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp /usr/local/bin/python3
```

**Verificação:**

```bash
/usr/libexec/ApplicationFirewall/socketfilterfw --getappblocked /usr/local/bin/python3
```

Deve dizer que conexões de entrada são permitidas. Cuidado: autorizar o `python3` do
Homebrew libera **qualquer** script Python seu que abra porta — é o preço do modelo por
aplicativo. Se o usuário não aceitar `sudo`, registre a pendência: local funciona, placa
não alcança.

## 8. (Opcional) Sensor do herdr

Só se o [herdr](https://herdr.dev) estiver instalado nesta máquina — ele melhora a
detecção de estado (lê a tela dos agentes) e habilita responder perguntas pelo painel.

O servidor procura o binário nesta ordem: `HERDR_BIN_PATH` → `PATH` → caminhos padrão
(`%LOCALAPPDATA%\Programs\Herdr\bin\herdr.exe`, `/usr/local/bin/herdr`,
`~/.local/bin/herdr`).

### Windows

```powershell
$herdr = "$env:LOCALAPPDATA\Programs\Herdr\bin\herdr.exe"
if (Test-Path $herdr) {
  [Environment]::SetEnvironmentVariable("HERDR_BIN_PATH", $herdr, "User")
  # reiniciar o servidor para o processo enxergar a variavel:
  Get-CimInstance Win32_Process -Filter "Name like 'pythonw%'" |
    Where-Object { $_.CommandLine -like "*claude_metrics_api*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
  Start-Process (Get-Command pythonw).Source -ArgumentList "`"$env:USERPROFILE\.claude\metrics-api\claude_metrics_api.py`"" -WorkingDirectory "$env:USERPROFILE\.claude\metrics-api"
}
```

`HERDR_BIN_PATH` existe porque `shutil.which("herdr")` a partir da pasta do servidor
acha o próprio `herdr.py` local (`.PY` está no `PATHEXT` do Windows) e o sensor morre
com `herdr.online: false`. A env var é checada ANTES do `which`.

### macOS

Nada a fazer no caso comum: a colisão do `PATHEXT` é exclusiva do Windows, e
`/usr/local/bin/herdr` já é um dos caminhos padrão. Confira e siga:

```bash
command -v herdr
```

**Exceção que dá trabalho:** `launchd` não herda o `PATH` do seu shell (o dele é
`/usr/bin:/bin:/usr/sbin:/sbin`). Se o herdr estiver fora dos caminhos padrão — o caso
típico é Apple Silicon, com `/opt/homebrew/bin/herdr` — o servidor sob o `launchd` não
o encontra mesmo com o `command -v` funcionando no seu terminal. Aí acrescente ao
`EnvironmentVariables` do plist (passo 6) e recarregue:

```xml
    <key>HERDR_BIN_PATH</key>
    <string>/opt/homebrew/bin/herdr</string>
```

```bash
launchctl bootout gui/$(id -u)/com.clawd.metrics-api 2>/dev/null
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.clawd.metrics-api.plist
```

**Verificação:** `/health` com `herdr.online: true` e `motor.atual: "herdr"`.
Sem herdr instalado: pule — o motor `hooks` cobre tudo, só sem leitura de tela.

## 9. Verificação de ponta a ponta

1. Abra uma sessão do Claude Code em qualquer repo desta máquina e mande um prompt.
2. Em até ~15 s:

```powershell
Invoke-RestMethod http://127.0.0.1:8787/status | ConvertTo-Json -Depth 4
```

```bash
curl -s http://127.0.0.1:8787/status | python3 -m json.tool
```

Critérios: `online: true`, `sessions >= 1`, `labels` com uma entrada contendo o repo
aberto, `session_pct`/`week_pct` preenchidos (a conta é a mesma do master — os números
devem bater com os de lá), e `tag` igual à do passo 5.

3. Descubra o IP LAN:

```powershell
(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like "192.168.*" -or $_.IPAddress -like "10.*" }).IPAddress
```

```bash
ipconfig getifaddr en0 || ipconfig getifaddr en1   # en0 = Wi-Fi na maioria dos Macs
```

4. Teste **de outra máquina** (idealmente do master):

```powershell
Invoke-RestMethod http://<IP-do-slave>:8787/status     # no master Windows
```

```bash
curl -s http://<IP-do-slave>:8787/status               # no master macOS/Linux
```

Chamar o próprio IP LAN de dentro do slave prova só que o bind é `0.0.0.0` — não prova
travessia de rede nem firewall. Se não houver acesso ao master no momento, faça o teste
local, diga isso no relatório e deixe o teste externo como pendência explícita.

## 10. Relatório final (obrigatório)

Entregue ao usuário:

```
Slave instalado nesta máquina.
- IP para o config.json do SD do master:  http://<IP>:8787/status
- Tag desta máquina (CLAWD_TAG): XXX
- /health: ok | /status: online com N sessão(ões)
- Firewall 8787: criado | desnecessário (firewall do macOS desligado) |
                 python3 autorizado (macOS) | PENDENTE (usuário recusou elevação)
- herdr: motor herdr ativo | ausente (motor hooks)
- statusline anterior: substituída (backup em statusline.cjs.bak) | não havia
- Auto-start no logon: criado (Startup\Claude Metrics API.lnk  |
                       ~/Library/LaunchAgents/com.clawd.metrics-api.plist)
- Teste a partir do master: ok | não feito (só o IP LAN local foi testado)

Próximo passo (na máquina master, feito pelo usuário ou pelo agente de lá):
adicionar no config.json do microSD da placa:
  "api": {
    "url":  "http://<IP-do-master>:8787/status",
    "url2": "http://<IP-deste-slave>:8787/status"
  }
Só o endereço — a tag desta máquina viaja no /status, não no cartão.
Requer firmware com suporte a url2 (spec "Dois PCs no painel", 2026-08-14).
Recomendação: fixe o IP deste PC (reserva DHCP no roteador) — se o IP mudar,
a placa perde o slave até o config.json ser corrigido.
```

## Troubleshooting

| sintoma | causa provável | correção |
|---|---|---|
| `/health` não responde | servidor não subiu | Windows: rode `python "$env:USERPROFILE\.claude\metrics-api\claude_metrics_api.py"` no terminal (sem `w`) e leia o erro. macOS: leia `~/.claude/metrics-api/api.err.log`, ou rode `python3 ~/.claude/metrics-api/claude_metrics_api.py` no terminal |
| porta 8787 ocupada | instância anterior viva | mate o processo do metrics-api (comando do passo 8 / `launchctl bootout` no macOS) e suba de novo; se for OUTRO programa, avise o usuário — a porta é fixa no servidor (`PORT = 8787` em `claude_metrics_api.py`) |
| `/status` sempre `online: false` | statusline não publica | confira `statusLine.command` no settings.json; feche e reabra a sessão do Claude Code; a linha de status precisa estar visível |
| sessão não muda de estado | hooks não registrados | rode de novo o script do passo 4 e reabra a sessão |
| placa/master não alcançam | firewall | passo 7; teste `Test-NetConnection <IP> -Port 8787` (Windows) ou `nc -vz <IP> 8787` (macOS) de fora |
| `herdr.online: false` com herdr instalado | Windows: `which` achou o `herdr.py` local. macOS: binário fora do `PATH` mínimo do `launchd` | passo 8 (`HERDR_BIN_PATH`) |
| agentes daqui aparecem no painel **sem tag** | `/status` sem o campo `tag` | cópia velha de `server/` (verificação do passo 1) ou servidor subiu sem `CLAWD_TAG`: confira `(Invoke-RestMethod http://127.0.0.1:8787/status).tag` |
| tag é a do hostname, não a escolhida | variável não estava no processo | Windows: repita o bloco do passo 6 com `$env:CLAWD_TAG` na frente. macOS: `CLAWD_TAG` faltando no `EnvironmentVariables` do plist — `.zshrc` e `launchctl setenv` não alcançam o agente |
| macOS: `Bootstrap failed: 5: Input/output error` | plist malformado ou caminho relativo | valide com `plutil -lint ~/Library/LaunchAgents/com.clawd.metrics-api.plist`; todo caminho tem que ser absoluto, sem `~` |
| macOS: servidor morre e volta em loop | `KeepAlive` religando um processo que quebra no import | `api.err.log` mostra o traceback; normalmente é arquivo faltando (passo 2) |
| atualizei `server/` no repo e nada mudou | o servidor roda de uma **cópia** | refaça o passo 2 (`Copy-Item`/`cp`) e reinicie o processo — `~/.claude/metrics-api/` não acompanha o repo |

## Desinstalar

```powershell
Remove-Item "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup\Claude Metrics API.lnk" -Force
Get-CimInstance Win32_Process -Filter "Name like 'pythonw%'" |
  Where-Object { $_.CommandLine -like "*claude_metrics_api*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Remove-Item "$env:USERPROFILE\.claude\metrics-api" -Recurse -Force
# settings.json: restaurar o backup settings.json.bak-esp32 (ou remover os hooks na mao)
# statusline: restaurar statusline.cjs.bak se existia
# firewall (admin): Remove-NetFirewallRule -DisplayName "Claude Metrics API 8787"
```

```bash
launchctl bootout gui/$(id -u)/com.clawd.metrics-api 2>/dev/null
rm -f ~/Library/LaunchAgents/com.clawd.metrics-api.plist
rm -rf ~/.claude/metrics-api
launchctl unsetenv CLAWD_TAG
# ~/.zshrc: remover a linha `export CLAWD_TAG=...`
# settings.json: restaurar o backup settings.json.bak-esp32 (ou remover os hooks na mao)
# statusline: restaurar statusline.cjs.bak se existia
# firewall: sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove /usr/local/bin/python3
```
