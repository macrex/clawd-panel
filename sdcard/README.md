# Conteúdo do cartão SD

Espelho do que a placa espera na raiz do cartão. Copie tudo daqui para a raiz do
SD.

```
/config.json          <- suas credenciais. NÃO está aqui (ver ../config.example.json)
/clawd/*.clw          <- animações do Clawd
```

> **O conteúdo de fábrica saiu do cartão** em 30/07/2026 — eram 288 MB de
> demos (`mjpeg`, `music`, `weather`, `pic`, `fonts`, `night7`) que a placa
> vinha com o firmware original. Backup íntegro, conferido arquivo por arquivo
> com SHA-256, em `../backup-cartao-fabrica/`. Essa pasta fica **fora do git**:
> tem arquivos únicos de 64 MB, acima dos limites do GitHub.

## `/clawd` — o caranguejo animado

Sprites do projeto [clawd-tank](https://github.com/marciogranzotto/clawd-tank)
de Marcio Granzotto Rodrigues, sob licença MIT (cópia em
`clawd/LICENSE-clawd-tank`). Convertidos por `../tools/sprites_to_clw.py`.

**As 19 animações estão no cartão**, não só as cinco em uso. São 1,1 MB num
cartão de 480 MB — e ter o acervo inteiro lá significa que uma melhoria futura
é só mudar o firmware, sem desmontar a placa para chegar no cartão.

### Em uso pelo firmware

| arquivo | quando aparece | frames | na tela | fps | CPU |
|---------|----------------|--------|---------|-----|-----|
| `alert.clw` | agente **bloqueado** | 40 | 240×196 | 10 | 64% |
| `typing.clw` | agente trabalhando | 12 | 172×156 | 8 | 51% |
| `idle.clw` | turno encerrado | 96 | 216×153 | 6 | 38% |
| `sleeping.clw` | estado desconhecido | 36 | 184×192 | 6 | 38% |
| `going_away.clw` | nenhuma sessão | 15 | 340×122 | 8 | 51% |

> **A tabela anterior deste arquivo dizia 6–20%, e estava errada.** Aqueles
> números vinham de escrever só o retângulo do sprite no painel, que era a ideia
> original. Essa placa **não aceita escrita parcial** — medido com blocos de
> teste nos quatro cantos do painel, os quatro caíram no mesmo ponto. Todo
> quadro custa um redesenho de tela inteira: **64 ms medidos**, independente do
> tamanho do sprite. O custo só é pago na página do Clawd; nas outras a
> animação não roda.

### Guardadas para depois

| arquivo | frames | na tela | uso plausível |
|---------|--------|---------|---------------|
| `debugger.clw` | 96 | 344×172 | agente rodando Read/Grep/Glob |
| `building.clw` | 8 | 248×150 | agente rodando Bash |
| `conducting.clw` | 16 | 152×156 | agente orquestrando subagentes |
| `wizard.clw` | 48 | 72×129 | agente em WebSearch/WebFetch |
| `beacon.clw` | 64 | 134×127 | comunicação MCP/LSP |
| `thinking.clw` | 32 | 152×184 | raciocínio longo |
| `confused.clw` | 48 | 152×113 | contexto acima de 80% |
| `happy.clw` | 20 | 248×178 | turno concluído sem bloqueio |
| `dizzy.clw` | 32 | 184×144 | erro / limite estourado |
| `sweeping.clw` | 12 | 414×168 | limpeza, compactação de contexto |
| `juggling.clw` | 10 | 204×198 | muitos agentes ao mesmo tempo |
| `walking.clw` | 8 | 240×160 | transição |
| `mini_crab.clw` | 16 | 40×28 | ícone, cabe no rodapé |
| `disconnected.clw` | 36 | 182×137 | **cuidado:** desenha um logo de Bluetooth enorme. No clawd-tank significa "perdi o link BLE com o computador", que não é o que falta aqui — por isso "nenhuma sessão" usa `going_away`. |

As seis primeiras são **por ferramenta**. Para usá-las, a API precisaria saber
qual ferramenta o agente está rodando: o hook `PreToolUse` daria isso, mas
dispara a cada chamada de ferramenta — bem mais pesado que os cinco hooks
instalados hoje.

**Sem estes arquivos a placa continua funcionando** — as páginas 1 e 2 ficam
idênticas, e a página 3 diz o que faltou. Uma animação ausente cai num
substituto em vez de derrubar a página.

## Regenerar

```bash
git clone --depth 1 https://github.com/marciogranzotto/clawd-tank.git /tmp/ct
python tools/sprites_to_clw.py /tmp/ct sdcard/clawd
```

O conversor lê tudo que existir em `firmware/main/assets/`, então um sprite novo
no clawd-tank passa a ser copiado sem editar nada. Cada frame é validado contra
`width × height` antes de gravar: header truncado ou erro de parsing viram erro
na hora, em vez de lixo na tela.

## Variantes econômicas

Os 17 sprites ImageGen também possuem variantes `_eco4.clw` (`110×50 @4×`)
e `_eco3.clw` (`146×66 @3×`). Todas usam 8 a 16 quadros, cadência de
167 ms para ambiente ou 125 ms para atenção, paleta chapada e no máximo
`0.30 B/px/qd`. Regenere as 34 variantes com:

```powershell
python tools\build_economy_collection.py
```

## Por que os `.clw` estão versionados

São artefatos derivados, o que normalmente ficaria fora do git. Aqui ficam
porque o conteúdo do cartão **é** parte da configuração da placa: sem eles, uma
máquina nova precisaria clonar outro repositório antes de gravar um cartão. São
1,1 MB e a licença permite a redistribuição.
