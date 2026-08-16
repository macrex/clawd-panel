# Estados climaticos do Clawd

Cinco animacoes originais em folhas 4x2, com oito quadros cada:

- `hot.png`: calor moderado, leque e suor;
- `cold.png`: frio, cachecol e respiracao visivel;
- `very_cold.png`: muito frio, casaco, touca, luvas e cristais;
- `very_hot.png`: muito calor, toalha fria, gelo e suor intenso;
- `swimwear.png`: roupa de banho, oculos e toalha de praia.
- `swim_briefs.png`: oculos escuros e sunga azul em V, sem outros objetos.

O corpo e a linha dos pes devem permanecer fixos. O importador normaliza as
dimensoes da folha para uma grade exata e rejeita compressao corporal ou
deslocamento vertical acima da tolerancia.

```powershell
python tools\import_climate_collection.py
```

Esta colecao gera apenas os arquivos originais, sem perfis ECO.
