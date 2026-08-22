#!/usr/bin/env python3
"""O corte de linha ANSI: contar coluna VISIVEL, nao byte."""

import sgr

ESC = "\x1b"


def vis(s):
    return sgr._visivel(s)


def test_limpar_guarda_sgr_e_descarta_o_resto():
    bruto = (f"{ESC}]0;titulo\x07"          # OSC: titulo de janela
             f"{ESC}[38;5;7moi{ESC}[0m"     # SGR: fica
             f"{ESC}[2J{ESC}[H"             # limpar tela e ir para casa: sai
             f"{ESC}(B")                    # charset: sai
    assert sgr.limpar(bruto) == f"{ESC}[38;5;7moi{ESC}[0m"


def test_corte_conta_coluna_e_nao_byte():
    # 10 letras dentro de ~40 bytes de escape. Cortar em 5 BYTES nao deixaria
    # uma letra na tela; em 5 COLUNAS deixa cinco.
    linha = "".join(f"{ESC}[38;5;{i}m{c}" for i, c in enumerate("abcdefghij"))
    partes = sgr.quebrar(linha, 5)
    assert len(partes) == 2
    assert vis(partes[0]) == "abcde"
    assert vis(partes[1]) == "fghij"


def test_cada_pedaco_carrega_a_cor_que_valia_nele():
    # A cor abre na coluna 1 e nunca mais e repetida pelo host. O segundo pedaco
    # tem que sair com ela, senao a placa desenha a continuacao em branco.
    linha = f"{ESC}[31m" + "x" * 10
    partes = sgr.quebrar(linha, 5)
    assert len(partes) == 2
    assert "31" in partes[1], "o pedaco de baixo perdeu a cor"
    assert vis(partes[1]) == "xxxxx"


def test_o_reset_no_meio_atravessa_o_corte():
    linha = f"{ESC}[31mabc{ESC}[0mdefgh" + "i" * 5
    partes = sgr.quebrar(linha, 5)
    # O segundo pedaco comeca depois do reset: nao pode carregar o vermelho.
    assert "31" not in partes[1]


def test_o_mesmo_atributo_nao_se_acumula():
    # Uma tela inteira de trocas de cor nao pode fazer o estado crescer sem fim:
    # so a ULTIMA cor de frente vale.
    estado = []
    for c in ("31", "32", "33", "34"):
        sgr._aplicar(estado, c)
    assert estado == ["34"]


def test_truecolor_entra_inteiro():
    estado = []
    sgr._aplicar(estado, "38;2;136;136;136")
    assert estado == ["38;2;136;136;136"]
    # E e substituido por outro fg, e nao somado a ele.
    sgr._aplicar(estado, "38;5;7")
    assert estado == ["38;5;7"]


def test_atributos_de_grupos_diferentes_convivem():
    estado = []
    sgr._aplicar(estado, "1")       # bold
    sgr._aplicar(estado, "31")      # vermelho
    sgr._aplicar(estado, "4")       # sublinhado
    assert set(estado) == {"1", "31", "4"}


def test_subparametro_por_dois_pontos_vira_ponto_e_virgula():
    estado = []
    sgr._aplicar(estado, "38:2::136:136:136")
    assert estado[0].startswith("38;2;")


def test_reducao_a_ascii_vem_antes_da_contagem():
    # "…" vira "..." e ganha duas colunas. Se a contagem viesse antes da
    # reducao, o corte cairia no lugar errado. (O caractere de caixa nao entra
    # mais nesta conta: ele e preservado — ver o teste logo abaixo.)
    linhas = sgr.preparar("a…b", 4)
    assert vis(linhas[0]) == "a..."
    assert vis(linhas[1]) == "b"


def test_linha_so_de_espacos_some():
    # Uma linha inteira de padding nao vira linha em branco no fim da tela: o
    # `preparar` derruba o rabo vazio, que e onde o buffer do terminal quase
    # sempre esta.
    assert sgr.preparar(f"{ESC}[31m" + " " * 80, 52) == []


def test_padding_a_direita_nao_vira_dez_linhas():
    # O herdr devolve a linha na largura do pane. Sem o rstrip, 63 caracteres
    # dentro de 838 virariam dezesseis linhas na placa.
    linha = f"{ESC}[32moi{ESC}[0m" + " " * 800
    fora = sgr.preparar(linha, 52)
    assert len(fora) == 1
    assert vis(fora[0]) == "oi"


def test_o_fim_vazio_e_descartado():
    fora = sgr.preparar("oi\n\n\n   \n", 52)
    # Toda linha sai com um reset na frente, mesmo sem cor: e ele que impede a
    # cor de uma linha vazar para a seguinte quando a placa desenha.
    assert len(fora) == 1
    assert vis(fora[0]) == "oi"


def test_saida_do_herdr_de_verdade():
    # Amostra real de `herdr pane read --format ansi`, com truecolor e 256.
    bruto = (f"{ESC}[0m{ESC}[38;2;136;136;136m" + "─" * 60 + f"{ESC}[0m\n"
             f"  {ESC}[0m{ESC}[38;5;8m❖{ESC}[0m{ESC}[38;5;7m clawd-panel{ESC}[0m\n")
    fora = sgr.preparar(bruto, 52)
    # 60 tracinhos em 52 colunas = dois pedacos, mais a linha do repo.
    assert len(fora) == 3, fora
    # A regua sai como o caractere de CAIXA, e nao reduzida a "-": a fonte
    # CP437 da placa tem o glifo, e quem converte e o lib/cp437 do firmware.
    assert vis(fora[0]) == "─" * 52
    assert vis(fora[1]) == "─" * 8
    assert "clawd-panel" in vis(fora[2])
    # A COR ABRE ANTES DA PRIMEIRA LETRA: os dois pedacos da regua tem que sair
    # com ela. Sem isso, a metade de cima da linha desenha em branco.
    assert "38;2;136;136;136" in fora[0], "o primeiro pedaco perdeu a cor"
    assert "38;2;136;136;136" in fora[1], "o segundo pedaco perdeu a cor"


def test_o_desenho_de_caixa_sobrevive_e_o_resto_nao():
    # A tela do Claude Code e 20% caractere de caixa (medido: 779 de 3.891).
    # Eles PASSAM, porque a fonte da placa os tem; acento e tipografia
    # continuam sendo reduzidos, porque ela nao os tem.
    fora = sgr.preparar("┌─┐ caçada — ok", 52)
    assert vis(fora[0]) == "┌─┐ cacada - ok"


def test_a_caixa_conta_uma_coluna_so():
    # Tres bytes em UTF-8, UMA coluna na tela. Contar byte aqui cortaria a
    # linha a um terco da largura.
    fora = sgr.preparar("─" * 60, 52)
    assert len(vis(fora[0])) == 52
    assert len(vis(fora[1])) == 8


def test_cols_zero_nao_estoura():
    assert sgr.quebrar("abc", 0) == []
    assert sgr.preparar("abc", 0) == []   # nada cabe em zero coluna
