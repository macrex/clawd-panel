# Coleção Imagegen

Folhas geradas com a ferramenta integrada de imagens e convertidas para
RGBA por chroma-key. A referência anatômica neutra fica em
`../../references/clawd_base/frame_000.png`.

## Arquivos

- `clawd_marks.png`: boné estrelado, martelo e foice, baseado na imagem enviada.
- `arachnid.png`: herói aracnídeo autoral com lançador de fio.
- `ruby_visor.png`: mutante com visor de energia rubi.
- `feral_energy.png`: brawler com projeções defensivas ciano.
- `stormcaller.png`: mutante climática com relâmpagos.
- `solar_mind.png`: telepata com aura de fogo solar.
- `magnetic_regent.png`: mutante com campos magnéticos e esferas metálicas.

Todas as folhas seguem o mesmo prompt estrutural: oito frames em grade 4x2,
pixel-art rígido, personagem completo, escala e baseline consistentes, sem
texto ou logotipo, em fundo chroma `#FF00FF`. Cada prompt acrescentou traje,
poder e sequência de poses próprios descritos pelos nomes acima.

## Compilar

```powershell
python tools\import_imagegen_sheet.py `
  sprites\sources\imagegen\arachnid.png `
  sdcard\clawd\arachnid.clw --frame-ms 100 --downsample 2 `
  --stabilize warm-core
```

Sprites mais altos (`stormcaller`, `solar_mind` e `magnetic_regent`) usam
`--downsample 2.3` para permanecer dentro dos 200 pixels verticais reservados
à animação no display.

## Dez ações do Clawd Marks

A pasta `marks_actions/` contém folhas transparentes para:

- `salute`, `march`, `hammering`, `reading` e `command`;
- `celebrate`, `rest`, `alert`, `speech` e `victory`.

O prompt-base preservou corpo coral, olhos retangulares, boné carvão com uma
estrela, martelo à esquerda e foice à direita. Cada variação acrescentou apenas
a ação descrita no nome, com chroma `#FF00FF`. As ações comuns usam oito
frames em grade 4x2. `rest.png` usa doze frames em grade 4x3 e 220 ms por
quadro para um ciclo de sono lento de 2,64 segundos.

Para recompilar todas de uma vez:

```powershell
python tools\import_marks_actions.py
```

O importador alinha o topo do boné entre os frames. Isso corrige folhas
em que a segunda linha foi gerada alguns pixels acima ou abaixo e evita o
efeito de salto sem congelar braços, pernas, ferramentas ou efeitos.

O compilador individual usa `--stabilize auto` por padrão. Depois de alinhar,
ele compara a altura corporal central dos quadros e interrompe a geração
se detectar compressão acima do limite configurado. A mensagem informa os quadros
defeituosos. `--allow-deformation` existe apenas para squash-and-stretch
intencional; não deve ser usado para contornar uma folha defeituosa.

`Marks: Descanso` usa a validação rígida de 10%. A tolerância genérica é
maior porque objetos diante do corpo, como o livro de `reading`, podem reduzir
a medida aparente sem que o personagem tenha sido comprimido.

Para recompilar toda a coleção ImageGen (os sete personagens e as dez ações)
com a âncora apropriada para cada desenho:

```powershell
python tools\import_imagegen_collection.py
```

## Acoes do Clawd Original

A pasta `original_actions/` contem tres folhas 4x2 do personagem neutro:
contando dinheiro, jogando notas fora e comendo tokens. Elas sao compiladas
somente na qualidade original, sem variantes economicas:

```powershell
python tools\import_original_actions.py
```

## Estados climaticos

A pasta `climate/` contem cinco folhas 4x2: calor, frio, muito frio, muito
calor e roupa de banho. O importador normaliza dimensoes fracionadas vindas
do gerador e ativa a verificacao estrita da linha dos pes:

```powershell
python tools\import_climate_collection.py
```

## Progressao dos niveis 1-99

`level_progression/checkpoints/` guarda onze marcos visuais gerados com a
ferramenta integrada. O nivel 1 reutiliza a anatomia original sem equipamento.
O gerador combina os marcos com melhorias cumulativas e cria uma fonte 4x2
revisavel e um `.clw` de oito quadros para cada nivel:

```powershell
python tools\build_level_progression.py
```

O maior componente coral precisa ocupar exatamente a mesma caixa nos oito
quadros. A compilacao para imediatamente se detectar salto, squash, stretch
ou mudanca de escala. Esta colecao tambem e exclusivamente original, sem ECO.

## Perfis econômicos

O motor aceita quatro perfis determinísticos. Eles usam nearest-neighbor,
paleta compartilhada sem dithering, 8 a 16 quadros e reduzem as cores até
atingir no máximo `0.30 B/px/qd`:

| perfil | nativo | escala de tela | uso |
|---|---:|---:|---|
| `eco4` | 110×50 | 4× | menor arquivo, até 440×200 na tela |
| `eco3` | 146×66 | 3× | mais detalhe, até 438×198 na tela |
| `footer2` | 110×42 | 1× | fonte de 84 px reduzida por 2 |
| `footer3` | 146×42 | 1× | fonte de 126 px reduzida por 3 |

As cadências disponíveis são `environment` (`167 ms`) e `attention`
(`125 ms`). Nenhum perfil ultrapassa o teto nativo de 220×100.

Gerar diretamente uma folha no perfil econômico:

```powershell
python tools\import_imagegen_sheet.py fonte.png saida.clw `
  --economy-profile eco4 --economy-motion attention `
  --economy-frames 8 --economy-colors 12 --max-bppf 0.30
```

Gerar `eco4` e `eco3` para os sprites ImageGen antigos que ainda permitem
variantes economicas:

```powershell
python tools\build_economy_collection.py
```

Entradas com `"economy_enabled": false` no manifesto sao ignoradas. As acoes
novas do Clawd Original usam essa opcao e permanecem somente na qualidade
original.

O conversor independente também aceita qualquer CLW já compilado:

```powershell
python tools\sprite_economy.py entrada.clw saida.clw `
  --profile footer2 --motion environment
```
