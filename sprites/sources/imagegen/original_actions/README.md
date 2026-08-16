# Acoes do Clawd Original

Tres folhas animadas em grade 4x2, geradas a partir da anatomia neutra do
Clawd e salvas com transparencia:

- `counting_money.png`: separa notas genericas entre duas pilhas;
- `throwing_money.png`: arremessa notas para o lado e acompanha a queda;
- `eating_tokens.png`: leva tokens dourados ate uma pequena boca temporaria.

Todas preservam o corpo coral, os olhos quadrados, as quatro pernas e a linha
dos pes. As notas nao possuem moeda, texto, marca ou denominacao.

## Compilar

```powershell
python tools\import_original_actions.py
```

O importador gera somente os tres arquivos `.clw` originais. Esta colecao nao
produz variantes ECO3, ECO4 ou de rodape.
