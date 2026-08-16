# Fundos animados

O `background_atlas.png` é a arte-base gerada pelo ImageGen integrado. Ele traz
seis cenários em uma grade 2×3: cidade cyberpunk chuvosa, praia ao pôr do sol,
oficina hacker, neve sob aurora, forja vulcânica e base lunar.

`tools/build_animated_backgrounds.py` recorta o atlas, converte cada cenário em
pixel-art 240×160 e cria oito quadros de movimento ambiente. A câmera, o
horizonte e o chão não se movem; só chuva, reflexos, monitores, neve, faíscas e
luzes variam. Cada folha final é 4×2 e origina um `.clw` 240×160 @2×, preenchendo
exatamente a tela 480×320.

Regeneração:

```powershell
python tools\build_animated_backgrounds.py
```

## Progressão 1–99

`level_up/` contém dez artes-base independentes e suas folhas animadas, uma para
cada faixa de nível: 1–9, 10–19, …, 90–99. Os arquivos são gerados com:

```powershell
python tools\build_level_backgrounds.py
```

## Temas

`themed/` reúne as artes-base e folhas animadas de reino lendário, arena de
energia, caverna do portal, templo chinês e planície da cerejeira. Regere com:

```powershell
python tools\build_themed_backgrounds.py
```
