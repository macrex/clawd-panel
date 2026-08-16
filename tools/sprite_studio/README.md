# Clawd Sprite Studio

Galeria local dos arquivos `.clw` usados pelo ESP32-S3. O navegador decodifica
diretamente RGB565 + RLE, então a prévia usa o mesmo binário copiado para o SD.

```powershell
python tools/sprite_studio/server.py --open
```

Sem `--open`, acesse <http://127.0.0.1:8765>.

O catálogo é mantido em cache e invalidado automaticamente quando um `.clw`
ou manifesto muda. Assim coleções grandes, como a progressão 1-99, não são
decodificadas novamente a cada recarregamento da página.

Os itens da categoria **Fundos animados** ocupam a tela 480×320 inteira. Ao
selecionar um deles, o seletor “Clawd sobre o fundo” permite revisar a composição
com qualquer personagem do acervo, animando as duas camadas no ritmo próprio.
Ao escolher um dos Clawds de nível 1–99, o Studio seleciona automaticamente o
fundo da faixa correspondente.

Os itens da categoria **Fundos animados** ocupam a tela 480×320 inteira. Ao
selecionar um deles, o seletor “Clawd sobre o fundo” permite revisar a composição
com qualquer personagem do acervo, animando as duas camadas no ritmo próprio.

Para compilar uma sequência nova de PNGs:

```powershell
python tools/build_sprite.py .\frames\eureka .\sdcard\clawd\eureka.clw `
  --frame-ms 125 --scale auto
```

O compilador precisa de Pillow (`python -m pip install Pillow`). O servidor e o
visualizador usam apenas a biblioteca padrão do Python.

O comportamento editorial da galeria fica em `sprites/manifest.json`. Arquivos
novos aparecem automaticamente mesmo antes de receberem uma entrada no
manifesto.

Ao selecionar um sprite que tenha `source_file` no manifesto, o inspetor exibe
o arquivo de origem. Folhas ImageGen 4x2 recebem uma grade clicável numerada de
1 a 8, permitindo registrar com precisão qual quadro precisa ser corrigido.
O acervo atual mantém fonte local revisável para todos os 45 sprites; SVGs são
mostrados como fonte vetorial e as folhas PNG mostram a numeração dos quadros.

## Importar a coleção vetorial complementar

Com os repositórios `esp32-s3` e `clawd-tank` lado a lado, instale a dependência
de renderização e gere os nove sprites adicionais:

```powershell
python -m pip install playwright
python tools\import_clawd_collection.py
```

O importador usa o Edge/Chromium local, captura os ciclos CSS/SMIL no tempo
exato, encaixa as cores na paleta original e compila diretamente para CLW. Para
regenerar apenas alguns itens:

```powershell
python tools\import_clawd_collection.py --only eureka grooving wake
```

## Compilar uma folha criada por imagem

Folhas PNG com transparência e grade 4x2 podem ser compiladas diretamente:

```powershell
python tools\import_imagegen_sheet.py `
  sprites\sources\imagegen\arachnid.png `
  sdcard\clawd\arachnid.clw --frame-ms 100 --downsample 2 `
  --stabilize warm-core
```

O recorte considera a união dos oito frames, preserva o centro do personagem
e escolhe automaticamente a escala inteira adequada ao display. Use
`--stabilize dark-cap` para personagens com o boné do Marks e
`--stabilize warm-core` para os demais personagens coral/vermelhos. Para
recompilar as 17 imagens geradas com a configuração correta de cada uma:

```powershell
python tools\import_imagegen_collection.py
```

Sem informar `--stabilize`, o modo `auto` procura o boné ou o núcleo coral,
remove deslocamentos verticais e valida compressão corporal antes de substituir
o arquivo CLW. O compilador mostra os números dos quadros reprovados.

## Revisar versões econômicas

Variantes com sufixo `_eco4` ou `_eco3` herdam automaticamente a fonte e os
metadados do sprite original. O Studio mostra o perfil, o indicador de
conformidade e a medida `B/px/qd`; a categoria **Econômico** filtra apenas
essas versões.
