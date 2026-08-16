# Fontes vetoriais

Cópia local dos SVGs em `clawd-tank/assets/svg-animations`. O Sprite Studio
serve estes arquivos somente para revisão visual no localhost. Manter a cópia
dentro deste projeto evita acesso do servidor a caminhos externos e preserva a
origem dos binários CLW.

O importador complementar atualiza automaticamente os SVGs que ele utiliza:

```powershell
python tools\import_clawd_collection.py
```
