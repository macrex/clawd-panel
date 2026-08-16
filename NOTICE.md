# Origem, créditos e licenças

## De onde este projeto vem

O painel começou em **[n1mb3/esp32-s3](https://github.com/n1mb3/esp32-s3)**, de
**Nimbcorp**, e este repositório é a continuação de um fork dele. A medição por
`git blame` sobre o código atual, em agosto de 2026:

| autor | linhas | participação |
|---|---|---|
| Nimbcorp (projeto original) | 20.354 | 67,4% |
| Guilherme Gabriel | 9.803 | 32,5% |

A maior parte do que está aqui, portanto, é obra do autor original. O projeto
de origem **não declara licença**, e por isso este repositório também não
declara uma: sem permissão do autor não há como conceder direitos sobre a
parte que é dele. Na prática, o código está disponível para leitura e não há
licença de uso, cópia ou redistribuição concedida por este repositório.

Se você quer usar este código, o caminho é falar com o autor do projeto
original.

## Licenças de terceiros

O que vem de fora está listado abaixo, com a licença de cada um.

## Arte do Clawd — clawd-tank

As animações do caranguejo (`sdcard/clawd/*.clw` e as folhas em
`sprites/sources/`) derivam do projeto
[clawd-tank](https://github.com/marciogranzotto/clawd-tank), de **Marcio
Granzotto Rodrigues**, sob licença MIT. A cópia da licença está em
`sdcard/clawd/LICENSE-clawd-tank` e acompanha a arte no cartão.

O que este repositório fez com ela: converter os quadros para o formato `.clw`
(RGB565 com compressão por corridas, ver [Formato `.clw`](#) no README),
recortar poses, montar ciclos e derivar variações. O desenho é dele; o
compilador e o firmware que o desenham são deste projeto.

## Arte que NÃO está aqui

Sprites derivados de material de terceiros com direitos reservados — South
Park, Sonic e afins — foram usados durante o desenvolvimento e **nunca entraram
no repositório**, nem no histórico. Eles moram apenas no cartão SD de quem os
gerou, e o `.gitignore` mantém essa fronteira:

```
sdcard/clawd/sonic_*.clw
sdcard/clawd/sp_*.clw
sprites/sources/south_park/
```

Os compiladores desses sprites (`tools/build_reset_kenny.py`,
`tools/build_reset_cartman.py`, `tools/build_token_offline.py`) continuam no
repositório porque o código é original — eles simplesmente não têm o que
compilar sem a arte, e falham com uma mensagem dizendo isso.

## Fundos e telas geradas

As imagens em `sprites/sources/imagegen/` foram geradas por modelo de imagem a
partir de prompts escritos dentro do projeto.

## Bibliotecas

| biblioteca | uso | licença |
|---|---|---|
| [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) | driver do painel AXS15231B e o canvas | ver o repositório da biblioteca |
| [ArduinoJson](https://arduinojson.org/) | parse do `/status` na placa | MIT |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | testes nativos, via PlatformIO | MIT |
| [pioarduino](https://github.com/pioarduino/platform-espressif32) | plataforma ESP32 com Arduino core 3.x | Apache-2.0 |

O servidor Python usa apenas a biblioteca padrão — nenhuma dependência de
`pip`, de propósito: ele sobe no logon e não pode depender de um ambiente
virtual que alguém esqueceu de ativar.

## Hardware

A placa **Guition JC3248W535** é produto de terceiro. Este repositório não
redistribui o firmware de fábrica dela nem o conteúdo original do cartão: se
você pretende gravar a placa, tire antes o seu próprio backup — a imagem
inteira sai com `esptool read-flash 0 0x1000000 fabrica.bin`, e o cartão se
copia como qualquer pendrive.
