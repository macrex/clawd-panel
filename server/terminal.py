#!/usr/bin/env python3
"""A tela de um agente, preparada para caber na tela da placa.

POR QUE ISTO EXISTE
O painel mostra o ESTADO dos agentes: percentual, repo, modelo, e agora a
pergunta de quem travou. O que ele nao mostrava e o que o agente escreveu — e
quando algo da errado e justamente o texto que responde.

O QUE ELE FAZ, EM UMA FRASE
Le a tela do pane pela CLI do herdr, traduz para o que a fonte da placa desenha
(ver texto.py), quebra na largura do painel e devolve so a janela pedida.

A TRAVA DE RESOLUCAO
O texto so cabe porque o pane e TRAVADO na largura da tela da placa enquanto ela
esta lendo: `herdr terminal session control <pane> --cols N --rows M` mantem o
pty naquele tamanho (TIOCSWINSZ de verdade) e devolve o tamanho original quando
o processo morre. Sem isso o texto chega na largura do host — 186 colunas
medidas aqui — e a quebra deste lado transforma cada regua em tres linhas de
tracinhos e desalinha toda tabela.

A ideia e do `herdr-assist` (walcew/herdr-assist, MIT), que faz o mesmo pelo
socket. Exige herdr >= 0.8.0: no 0.7.1 o subcomando nao existe.

O EFEITO COLATERAL, QUE E VISIVEL
Enquanto o painel esta lendo, a janela daquele agente no seu terminal fica com
a largura da placa. Ele volta ao normal sozinho — ao sair da tela, por
inatividade (ver LEASE_S) ou se esta API morrer, porque o filho ve EOF no stdin
e se desanexa. E o EOF que torna isso seguro: nao ha caminho em que a trava
sobreviva a quem a criou.

POR QUE UMA CONCESSAO COM PRAZO, E NAO UM PAR ABRIR/FECHAR
O painel nao tem conexao persistente com esta API: ele pergunta e desliga. Se a
trava dependesse de um "fechar" explicito, um painel que perdesse energia com a
tela aberta deixaria o terminal do usuario estreito para sempre. Cada leitura
renova a concessao e o silencio a encerra.
"""

import json
import os
import subprocess
import threading
import time

from texto import so_ascii

# Sem isto, cada trava pisca uma janela de console: a API roda sob pythonw.exe,
# sem console proprio. Mesma razao do SEM_JANELA do herdr.py.
_SEM_JANELA = 0x08000000 if os.name == "nt" else 0

# De onde ler. "visible" e o viewport — o que esta na tela AGORA, ja reflowado
# pelo emulador na largura que travamos. Com "recent" viria o historico na
# largura antiga, que e justamente o que a trava existe para evitar.
SOURCE = "visible"

# A resposta vai pela rede para uma placa com PSRAM contada. Os tetos existem
# porque `cols` e `rows` chegam do painel, ou seja, da REDE: sem eles, um pedido
# de 9999 linhas viraria uma resposta de megabytes.
COLS_MAX = 160
ROWS_MAX = 60

# Quanto tempo a trava sobrevive sem uma leitura nova. O painel pede a tela a
# cada dois segundos; quatro pedidos perdidos e mais do que uma rede ruim
# justifica, e menos do que alguem levaria para notar o terminal estreito.
LEASE_S = 8.0

# Tempo para a TUI redesenhar depois de mudar de tamanho. Sem esta pausa a
# primeira leitura pega a tela ANTES do reflow — a largura nova com o conteudo
# velho, que e a pior das duas.
ASSENTAR_S = 0.4

# Idem depois de rolar: o app recebe a roda, rola e redesenha.
ROLAGEM_ASSENTA_S = 0.15

_trava = {"pane": None, "tam": None, "proc": None, "ts": 0.0}
_lock = threading.RLock()
_vigia = None


def esquecer():
    """Solta a trava e zera o estado. Os testes chamam; producao, nunca."""
    soltar()
    with _lock:
        _trava.update({"pane": None, "tam": None, "proc": None, "ts": 0.0})


def preparar(bruto, cols):
    """Linhas prontas para desenhar: ASCII, sem padding, quebradas em `cols`.

    A quebra e DURA e nao por palavra. Conteudo de terminal e alinhado por
    coluna — quebrar na palavra desalinharia tabela, diff e barra de progresso,
    que sao justamente o que se quer ler aqui.
    """
    if not bruto:
        return []
    if cols <= 0:
        cols = COLS_MAX

    fora = []
    for linha in so_ascii(bruto.replace("\t", "    ")).split("\n"):
        # O herdr devolve a linha na largura do pane: medido aqui, 63
        # caracteres de texto dentro de uma linha de 838. Sem este rstrip, cada
        # linha de conteudo viraria uma dezena de linhas em branco.
        linha = linha.rstrip()
        if not linha:
            fora.append("")
            continue
        while linha:
            fora.append(linha[:cols])
            linha = linha[cols:]

    # O fim do buffer do terminal e quase sempre vazio. Mante-lo gastaria a tela
    # inteira mostrando nada — e a tela abre justamente no fim.
    while fora and not fora[-1]:
        fora.pop()
    return fora


def janela(linhas, rows, offset):
    """`rows` linhas terminando `offset` linhas antes do fim.

    `offset` 0 e o FIM, que e onde esta o que acabou de acontecer e onde a tela
    abre. Rolar para tras aumenta o offset.

    Passar do inicio para no inicio em vez de devolver vazio: uma tela preta no
    exato momento em que o usuario chegou ao comeco do historico parece defeito.
    """
    total = len(linhas)
    if total == 0 or rows <= 0:
        return [], total
    if offset < 0:
        offset = 0
    fim = total - offset
    if fim < min(rows, total):
        fim = min(rows, total)
    inicio = max(0, fim - rows)
    return linhas[inicio:fim], total


# ---- A trava de resolucao ----
# Daqui para baixo este modulo AGE sobre o terminal do usuario: ele muda o
# tamanho de um pane de verdade. Tudo aqui existe para que essa mudanca seja
# sempre temporaria e sempre reversivel sem intervencao.

def _bin():
    import herdr
    return herdr._binario_ativo or herdr.achar_binario()


def soltar():
    """Devolve o pane ao tamanho da UI do herdr. Silencioso se nao ha trava."""
    with _lock:
        proc, pane = _trava["proc"], _trava["pane"]
        _trava.update({"pane": None, "tam": None, "proc": None, "ts": 0.0})
    if not proc or proc.poll() is not None:
        return
    try:
        # `terminal.release` e a saida limpa. Fechar o stdin faz o mesmo pelo
        # EOF, e por isso o `close` vem logo atras: se o processo ignorar o
        # comando, o EOF ainda o desanexa.
        proc.stdin.write(b'{"type":"terminal.release"}\n')
        proc.stdin.flush()
        proc.stdin.close()
        proc.wait(timeout=2)
    except (OSError, ValueError, subprocess.TimeoutExpired):
        try:
            proc.kill()
        except OSError:
            pass


def _vigiar():
    """Solta a trava quando a concessao vence.

    Existe porque o painel NAO tem conexao persistente com esta API: ele
    pergunta e desliga. Sem este vigia, um painel que perdesse energia com a
    tela aberta deixaria o terminal do usuario estreito ate alguem reparar.
    """
    while True:
        time.sleep(1.0)
        with _lock:
            vencida = _trava["proc"] and (time.time() - _trava["ts"]) > LEASE_S
        if vencida:
            soltar()


def travar(pane_id, cols, rows, agora=None):
    """Trava o pane em cols x rows. Devolve (esta_travado, acabou_de_subir).

    Dois booleanos e nao um porque as duas perguntas tem consumidores
    diferentes: `esta_travado` vai para o painel, que avisa quando a tela nao
    pode ser confiada; `acabou_de_subir` decide se e preciso esperar a TUI
    reflowar. Com um so, "ja estava de pe" e "nao consegui" ficariam iguais.

    O herdr redimensiona o pty de verdade e devolve o tamanho quando este
    processo morre. O stdin em pipe e o cinto de seguranca: se esta API cair de
    qualquer jeito, o filho ve EOF e se desanexa sozinho — nao ha caminho em
    que a trava sobreviva a quem a criou.
    """
    global _vigia
    agora = agora or time.time
    with _lock:
        igual = (_trava["pane"] == pane_id and _trava["tam"] == (cols, rows)
                 and _trava["proc"] and _trava["proc"].poll() is None)
        if igual:
            _trava["ts"] = agora()          # renova a concessao
            return True, False

    soltar()
    binario = _bin()
    if not binario:
        return False, False
    try:
        proc = subprocess.Popen(
            [binario, "terminal", "session", "control", pane_id,
             "--cols", str(cols), "--rows", str(rows)],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,      # os quadros do controller nao servem aqui
            stderr=subprocess.DEVNULL,
            creationflags=_SEM_JANELA)
    except OSError:
        return False, False

    with _lock:
        _trava.update({"pane": pane_id, "tam": (cols, rows), "proc": proc,
                       "ts": agora()})
        if _vigia is None:
            _vigia = threading.Thread(target=_vigiar, daemon=True)
            _vigia.start()
    return True, True


def travado(pane_id):
    """A trava deste pane esta DE PE agora?

    Nao basta o Popen ter dado certo: o controller morre em seguida quando o
    pane nao existe (visto ao usar um id antigo depois de o herdr reiniciar) ou
    quando outro cliente ja o controla. Sem esta checagem o painel dizia que a
    largura estava ajustada enquanto o processo ja tinha morrido — mentira
    exatamente no aviso que existe para explicar a tela quebrada.
    """
    with _lock:
        proc = _trava["proc"]
        return bool(proc and proc.poll() is None and _trava["pane"] == pane_id)


def rolar(pane_id, linhas, col=0, row=0):
    """Rolagem NATIVA: uma roda de mouse no pane travado.

    Quem decide o destino e o herdr: app com mouse tracking (o Claude Code tem)
    recebe o evento e rola o proprio conteudo; sem isso, anda o scrollback do
    emulador. Rolar deste lado nao daria nem uma coisa nem outra, porque com a
    trava so o viewport e lido.

    A posicao nao e detalhe: com o ponteiro fora da area de transcript o Claude
    Code ignora a roda. Por isso o default e o canto de cima, dentro do texto.
    """
    with _lock:
        proc = _trava["proc"]
        if _trava["pane"] != pane_id or not proc or proc.poll() is not None:
            return False
        cols, rows = _trava["tam"]
    cmd = {"type": "terminal.scroll",
           "direction": "up" if linhas < 0 else "down",
           "lines": abs(int(linhas)), "source": "wheel",
           "column": max(0, min(col, cols - 1)),
           "row": max(0, min(row, rows - 1))}
    try:
        proc.stdin.write((json.dumps(cmd) + "\n").encode())
        proc.stdin.flush()
    except (OSError, ValueError):
        return False
    return True


def ler(pane_id, cols, rows, rolagem=0, ler=None, agora=None, dormir=None):
    """Bloco pronto para o GET /terminal.

    `rolagem` e um DELTA em linhas, aplicado uma vez: negativo volta no
    historico, positivo avanca. Delta e nao posicao absoluta porque quem guarda
    a posicao e o proprio terminal do host — aqui nao ha como saber onde ele
    esta, e fingir que ha produziria um numero que discorda da tela.
    """
    import herdr

    ler = ler or herdr.ler_pane
    agora = agora or time.time
    dormir = dormir or time.sleep

    cols = max(1, min(int(cols or COLS_MAX), COLS_MAX))
    rows = max(1, min(int(rows or ROWS_MAX), ROWS_MAX))
    rolagem = int(rolagem or 0)

    if not pane_id:
        # Sem pane a tela esta fechada: e a deixa para devolver o terminal ao
        # tamanho dele. E o caminho normal de saida, e nao um caso de erro.
        soltar()
        return {"pane_id": "", "linhas": [], "cols": cols, "rows": rows,
                "travado": False}

    ok, subiu = travar(pane_id, cols, rows, agora=agora)
    if subiu:
        dormir(ASSENTAR_S)
        # Confere DEPOIS da pausa, e nao no retorno do Popen: o controller morre
        # em seguida quando o pane nao existe ou quando outro cliente ja o
        # controla, e no instante do Popen isso ainda nao aconteceu.
        ok = travado(pane_id)
    if rolagem and rolar(pane_id, rolagem):
        dormir(ROLAGEM_ASSENTA_S)

    bruto = ler(pane_id, rows, "visible")
    linhas, _ = janela(preparar(bruto, cols), rows, 0)
    return {"pane_id": pane_id, "linhas": linhas, "cols": cols, "rows": rows,
            # O painel avisa quando a trava nao subiu: sem ela o texto vem na
            # largura do host e a tela fica ilegivel sem explicacao.
            "travado": bool(ok)}
