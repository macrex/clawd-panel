# Logotipos dos provedores

Os SVGs de origem dos ícones que aparecem no cabeçalho de grupo do card de
sessões — um por CLI que o painel distingue, mais um reserva. Quem os compila é
`tools/build_icone_provedor.py`; quem os desenha é `src/provedores.cpp`, e os
arrays daquele arquivo **são a saída do compilador**, não arte editada à mão.

| Arquivo | Marca | Origem |
|---|---|---|
| `claude-code.svg` | Claude Code (Anthropic) | ícone oficial |
| `antigravity.svg` | Antigravity (Google) | ícone oficial, **só a silhueta** |
| `codex.svg` | Codex / OpenAI | ícone oficial |
| `ollama.svg` | Ollama — **o reserva**, vale por toda CLI sem arte própria | ícone oficial (simple-icons), **preenchido** |

## Por que o Antigravity está reduzido

O SVG oficial tem 2,4 MB: a silhueta é um path, e todo o resto do arquivo são
imagens raster embutidas em base64 que pintam o degradê de dentro dela. O
compilador só usa a **cobertura** — o degradê nunca chega ao firmware, porque o
que vai para a placa é uma máscara de alfa pintada na cor da marca.

Versionar 2,4 MB de degradê para gerar um ícone de 13×12 px seria desproporcional,
então aqui está só o path externo, com o mesmo `viewBox`. Foi conferido: as duas
versões dão máscaras de 512×472 idênticas em forma, com diferença máxima de 2 no
alfa depois da redução, e **nenhum** pixel muda de aceso para apagado no bitmap
final. Se um dia o degradê importar, ele está no press kit do Google.

## Por que o Ollama entra preenchido

A lhama é desenhada a traço: em 512 px o contorno tem 14, o que dá **um terço de
pixel** nos 12 px de altura do ícone. Não é falta de meio-tom como no Codex — não
sobra traço nenhum para o meio-tom guardar, e a redução direta devolve chuvisco.
O `modo="silhueta"` do compilador fecha o contorno e mantém furados os olhos, o
focinho e a narina: as orelhas dizem que é um bicho, e as ilhas vazadas dizem
qual. A base sai reta porque o `viewBox` oficial já corta o corpo ali.

## Uso

São marcas de terceiros, presentes aqui pelo mesmo motivo que o nome "Codex"
está escrito ao lado: identificar de qual CLI é cada grupo de sessões. Não são
arte do projeto e não devem ser modificadas — para mudar como um ícone aparece,
mexa na receita em `tools/build_icone_provedor.py` e recompile.
