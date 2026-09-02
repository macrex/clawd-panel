# Clawd Panel — instruções para agentes

Esta pasta é a **casa do projeto** — e a única. Todo trabalho acontece aqui:
código, sprites, servidor e runbooks. Uma versão anterior viveu em
`D:\workspace\esp32\esp32-s3` (fork privado `macrex/esp32-s3`); essa pasta foi
**apagada do disco em 02/09/2026** e a história ficou só no GitHub. Não a
reclone nem puxe nada de lá sem pedido explícito.

## O que é

Firmware ESP32-S3 (placa Guition JC3248W535, tela 320×480 com touch) + uma API
local em Python que exporta as métricas do Claude Code. A placa consome
`GET /status` pela Wi-Fi a cada 2 s. Ver `README.md` para a visão completa e
`NOTICE.md` para a origem do projeto.

## A armadilha que mais custa tempo

**A API em produção não roda desta pasta.** Ela roda de uma cópia instalada em
`%USERPROFILE%\.claude\metrics-api\`, disparada no logon por tarefa agendada.
Editar `server/*.py` aqui **não muda o painel**. Para valer:

```powershell
$inst = Join-Path $env:USERPROFILE ".claude\metrics-api"
Copy-Item "server\claude_metrics_api.py" "$inst\" -Force
Stop-Process -Id ((Get-NetTCPConnection -LocalPort 8787 -State Listen).OwningProcess | Select -First 1) -Force
Start-Process pythonw "$inst\claude_metrics_api.py" -WindowStyle Hidden
```

## Comandos

O PlatformIO não está no PATH; chame pelo caminho. **Build e gravação rodam no
PowerShell** — no Git Bash o toolchain do ESP32 falha (o `idf_tools.py` recusa
ambiente MSys).

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run          # firmware
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" test -e native   # 219 testes
cd server; python -m pytest -q                                            # 385 testes
python tools\tela.py                                                      # foto da tela da placa
python tools\atualizar_sprite.py sdcard\clawd\arquivo.clw                  # sprite novo pelo ar
```

Gravar a placa: a skill `esp32-flash` resolve a porta pelo VID/PID (`303A:1001`)
e devolve um veredito. **Se o boot disser `SEM CONFIG (cartão SD não montou)`,
não é o firmware**: o cartão travou em `0x107` e só um corte REAL de energia
resolve — USB fora e chave da bateria desligada. Reset por software não serve,
porque com bateria a placa nunca desliga de fato.

## Onde a lógica mora

- `src/` — o que depende do Arduino: laço, rede, tela, touch, cartão, sprites.
- `lib/` — aritmética pura, com teste nativo. **Regra da casa:** decisão que dá
  para testar no PC nasce aqui, não no `src/`. Ex.: `semSessao()` decide se o
  Clawd dorme; a UI só desenha.
- `test/` — as suítes nativas (Unity) de `lib/`.
- `server/` — a API, os hooks, o statusline e as ferramentas de PC.
- `tools/` — compiladores de sprite `.clw`, atualização pelo ar, captura.

## Fronteiras que não se cruzam

- **Arte de terceiros nunca entra no git.** `sdcard/clawd/sp_*.clw`,
  `sonic_*.clw` e `sprites/sources/south_park/` estão no `.gitignore` e moram só
  no cartão de quem os gera. Os compiladores ficam no repo; a arte, não.
- **`config.json` (senha do Wi-Fi), `server/state.json`, `server/weather.json` e
  `server/works.jsonl` também não.** Modelos: `config.example.json` e
  `weather.example.json`.
- Nada de caminho absoluto de máquina em script versionado — derive de
  `$env:USERPROFILE`.
