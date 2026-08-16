# Configurar a máquina MASTER do painel Clawd

> **Runbook para agente.** O master é a máquina em que a placa confia: cabeçalho,
> relógio, clima e limites saem do `/status` dela. Este arquivo cobre (A) a base de
> software, (B) o que só o master tem, (C) a placa (microSD + firmware) e (D) registrar
> um slave. Se a máquina já é master e você só quer plugar um PC2, pule para (D).

## A. Base de software

Idêntica à do slave. Execute os passos **0 a 8 de [SLAVE.md](SLAVE.md)** nesta máquina —
servidor em `~/.claude/metrics-api/`, statusline, hooks, auto-start, firewall `8787`,
herdr opcional, verificação. Lá cada passo tem o bloco Windows e o bloco macOS; siga o
da sua plataforma. Volte aqui ao terminar.

Os blocos deste arquivo seguem a mesma convenção: **PowerShell primeiro, zsh (macOS)
logo abaixo**. A seção C (a placa) é a única que assume um PC ligado por USB — o resto
é igual nos dois sistemas.

## B. Só no master

### B1. Clima (opcional, recomendado)

O cabeçalho do painel mostra hora/data/temperatura vindas do master.

```powershell
Copy-Item "$env:USERPROFILE\.claude\metrics-api\weather.example.json" "$env:USERPROFILE\.claude\metrics-api\weather.json"
notepad "$env:USERPROFILE\.claude\metrics-api\weather.json"   # cidade/latitude/longitude
```

```bash
cp ~/.claude/metrics-api/weather.example.json ~/.claude/metrics-api/weather.json
open -e ~/.claude/metrics-api/weather.json    # cidade/latitude/longitude
```

Formato e provedores: seção de clima do [`server/README.md`](../server/README.md).
Reinicie o servidor após editar (macOS: `launchctl kickstart -k
gui/$(id -u)/com.clawd.metrics-api`). **Verificação:** `/status` com bloco
`weather`/`clock` preenchido.

### B2. IP estável

A placa grava a URL do master no microSD. Fixe o IP desta máquina (reserva DHCP no
roteador). Anote-o:

```powershell
(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like "192.168.*" -or $_.IPAddress -like "10.*" }).IPAddress
```

```bash
ipconfig getifaddr en0 || ipconfig getifaddr en1   # en0 = Wi-Fi na maioria dos Macs
```

### B3. A tag desta máquina

Três caracteres que aparecem no chip de cada agente desta máquina, quando o painel
mostra mais de uma. Sem a variável, o servidor usa as três primeiras letras
alfanuméricas do hostname (`DESKTOP-EXEMPLO` → `DES`) — funciona, mas escolher é melhor.

```powershell
[Environment]::SetEnvironmentVariable("CLAWD_TAG","WIN","User")
# a variável só chega a processos novos: reinicie o servidor com ela no ambiente
$env:CLAWD_TAG = "WIN"
Get-CimInstance Win32_Process -Filter "Name like 'pythonw%'" |
  Where-Object { $_.CommandLine -like "*claude_metrics_api*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Process "C:\Python310\pythonw.exe" -ArgumentList "`"$env:USERPROFILE\.claude\metrics-api\claude_metrics_api.py`"" -WorkingDirectory "$env:USERPROFILE\.claude\metrics-api"
```

No macOS quem manda é o `EnvironmentVariables` do plist criado no passo 6 do SLAVE.md —
`~/.zshrc` e `launchctl setenv` não alcançam um agente `launchd`. Edite o plist e
recarregue:

```bash
# no plist: <key>CLAWD_TAG</key><string>MAC</string>
launchctl bootout gui/$(id -u)/com.clawd.metrics-api 2>/dev/null
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.clawd.metrics-api.plist
```

**Verificação:** `(Invoke-RestMethod http://127.0.0.1:8787/status).tag` devolve `WIN`
(macOS: `curl -s http://127.0.0.1:8787/status | python3 -c "import sys,json;print(json.load(sys.stdin)['tag'])"`).
Mais que 3 caracteres é cortado; pontuação é descartada.

## C. A placa

### C1. microSD

Na raiz do cartão (FAT32):

1. Pasta `sdcard/clawd/` do repo → copiar como `clawd/` na raiz do cartão (sprites/temas).
2. Criar `config.json` na raiz (modelo: `config.example.json` do repo):

```json
{
  "wifi": { "ssid": "SUA_REDE", "password": "SUA_SENHA" },
  "api":  { "url": "http://<IP-do-master>:8787/status" },
  "poll_ms": 2000,
  "brightness": 200
}
```

O cartão **não** guarda nome de máquina: quem se identifica é o serviço (`CLAWD_TAG`,
passo B3). Assim, acrescentar uma máquina não exige abrir a placa.

Wi-Fi de 2,4 GHz (o ESP32-S3 não vê 5 GHz). Inserir o cartão com a placa desligada.

### C2. Compilar e gravar o firmware

Ferramentas: PlatformIO (pioarduino) + esptool num **venv** (não instalar no Python
global). **No Windows, build SEMPRE via PowerShell nativo — nunca Git Bash/MSYS**: o
`idf_tools.py` do pioarduino recusa ambiente MSys (`MSYSTEM` setado) e o toolchain
xtensa falha na instalação, matando a build depois com
`'xtensa-esp32s3-elf-g++' não é reconhecido`. A armadilha não existe no macOS — o zsh
padrão serve.

> Os blocos macOS desta seção C2 são a tradução direta dos de Windows (venv em `bin/`
> em vez de `Scripts/`, porta `/dev/cu.*`); o toolchain xtensa tem build oficial para
> Intel e Apple Silicon. Ainda **não foram exercitados com a placa em mãos** — a
> instalação de referência grava pelo Windows. Ao gravar de um Mac pela primeira vez,
> corrija aqui o que divergir.

```powershell
python -m venv "$env:USERPROFILE\pio-venv"
& "$env:USERPROFILE\pio-venv\Scripts\pip" install platformio esptool
Set-Location <pasta-do-repo>
& "$env:USERPROFILE\pio-venv\Scripts\platformio" run
```

```bash
python3 -m venv ~/pio-venv
~/pio-venv/bin/pip install platformio esptool
cd <pasta-do-repo>
~/pio-venv/bin/platformio run
```

Detectar a porta da placa (USB nativo, `VID_303A&PID_1001` — tipicamente `COMx` no
Windows, `/dev/cu.usbmodem*` no macOS):

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, PNPDeviceID
```

```bash
ls /dev/cu.usbmodem*      # USB CDC nativo: o macOS reconhece sem driver de terceiro
```

Gravar:

```powershell
& "$env:USERPROFILE\pio-venv\Scripts\platformio" run -t upload --upload-port COM4
```

```bash
~/pio-venv/bin/platformio run -t upload --upload-port /dev/cu.usbmodem101
```

**Armadilha da bateria LiPo:** a placa tem bateria — gravar a flash NÃO garante
reinício. O esptool relata sucesso (hash confere), mas a placa segue rodando o firmware
antigo da RAM. Sintoma: o uptime do log serial (`pulso t=NNNs`, 115200 baud) não zera.
Force um reset real após gravar — abrir a porta serial já basta (o DTR do pyserial
reinicia), ou:

```powershell
& "$env:USERPROFILE\pio-venv\Scripts\esptool" --port COM4 --before usb-reset --after watchdog-reset flash-id
```

```bash
~/pio-venv/bin/esptool --port /dev/cu.usbmodem101 --before usb-reset --after watchdog-reset flash-id
```

**Verificação:** monitor serial a 115200 mostra `pulso t=` baixinho (uptime zerado) e
`http=200` — a placa está falando com o `/status` do master.

## D. Registrar um slave (PC2)

Pré-requisito: o slave instalado e verificado pelo [SLAVE.md](SLAVE.md) — você precisa
do IP dele e de `http://<IP-do-slave>:8787/status` respondendo daqui.

> **Dependência de firmware:** `url2`/`nome2` existem a partir do firmware da spec
> "Dois PCs no painel" (2026-08-14). Firmware anterior não conhece os campos e os
> ignora sem erro — o painel segue só com o master. Atualize o firmware (C2) para o
> slave aparecer.

1. Desligue a placa e retire o microSD.
2. Edite o `config.json`:

```json
{
  "wifi": { "ssid": "SUA_REDE", "password": "SUA_SENHA" },
  "api": {
    "url":  "http://<IP-do-master>:8787/status",
    "url2": "http://<IP-do-slave>:8787/status"
  },
  "poll_ms": 2000,
  "brightness": 200
}
```

Só endereços — as tags vêm do `/status` de cada máquina (`CLAWD_TAG` lá, passo B3 aqui
e passo 5 no [SLAVE.md](SLAVE.md)). Elas aparecem no primeiro chip de cada linha de
agente: a do master em verde, a da segunda em violeta — uma cor por máquina.

3. Recoloque o cartão e ligue a placa.

**Verificação:**

- Agentes das duas máquinas na lista; os do slave com barra violeta + chip `nome2`.
- Pausar o servidor do master (matar o `pythonw` do metrics-api; no macOS,
  `launchctl bootout gui/$(id -u)/com.clawd.metrics-api`) → em poucos segundos o
  selo `VIA PC2` pisca no cabeçalho e relógio/limites continuam andando (servidos pelo
  slave). Religar → selo some, agentes do master voltam.
- Desligar o slave → lista só local, sem o poll ficar visivelmente mais lento (backoff).

## Troubleshooting específico do master

| sintoma | causa provável | correção |
|---|---|---|
| tela `SEM CONFIG` | `config.json` ausente/inválido no cartão | a própria tela diz o campo que faltou; conferir vírgulas e aspas |
| `CLAUDIO OFFLINE` com servidor vivo | placa não alcança o IP | firewall (SLAVE.md passo 7), IP errado no `config.json`, Wi-Fi 5 GHz |
| firmware novo "não pegou" | reset fantasma da LiPo | C2, forçar reset real |
| slave configurado e nada de agentes dele | firmware sem suporte a `url2` | atualizar firmware (C2) |
| build falha com `xtensa-esp32s3-elf-g++ não é reconhecido` (Windows) | build rodou em Git Bash/MSYS | refazer em PowerShell nativo (C2) |
| macOS: `ls /dev/cu.usbmodem*` não lista nada | cabo só de energia, ou placa não enumerou | troque por um cabo de dados; a placa aparece como USB CDC nativo, sem driver |
