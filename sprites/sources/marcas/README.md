# Logotipos dos provedores

Os SVGs de origem dos três ícones que aparecem no cabeçalho de grupo do card de
sessões — um por CLI que o herdr reconhece. Quem os compila é
`tools/build_icone_provedor.py`; quem os desenha é `src/provedores.cpp`, e os
arrays daquele arquivo **são a saída do compilador**, não arte editada à mão.

| Arquivo | Marca | Origem |
|---|---|---|
| `claude-code.svg` | Claude Code (Anthropic) | ícone oficial |
| `antigravity.svg` | Antigravity (Google) | ícone oficial, **só a silhueta** |
| `codex.svg` | Codex / OpenAI | ícone oficial |

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

## Uso

São marcas de terceiros, presentes aqui pelo mesmo motivo que o nome "Codex"
está escrito ao lado: identificar de qual CLI é cada grupo de sessões. Não são
arte do projeto e não devem ser modificadas — para mudar como um ícone aparece,
mexa na receita em `tools/build_icone_provedor.py` e recompile.
