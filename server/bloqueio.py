#!/usr/bin/env python3
"""Le o formulario de aprovacao que esta na tela de um agente bloqueado.

POR QUE ISTO EXISTE
O herdr diz que um agente esta `blocked`, e para por ai: o `agent_status` dele e
derivado por regra sobre o texto do terminal e nao expoe QUAL e a pergunta nem
QUAIS sao as opcoes. Nao ha API para isso. Quem le a lista e este parser.

CREDITO
A logica vem do `herdr-assist` (walcew/herdr-assist, MIT), em
`plugin/herdr_bridge.py` — `parse_form`, `extract_question`, `strip_box`. La ela
roda sobre o socket unix do herdr; aqui roda sobre o texto que a CLI devolve,
porque no Windows o socket e um named pipe que a stdlib nao abre (ver o
cabecalho de herdr.py). As expressoes regulares foram calibradas contra telas
reais de Claude Code e Codex, e e por isso que este arquivo nao tenta reinventa-las.

ESTE MODULO NAO AGE
So le texto e devolve estrutura. Quem manda tecla e o herdr.py, e quem decide se
pode mandar e a API. A separacao existe porque o pior defeito de um leitor e
mentir, e o de um escritor e agir sem que voce queira — e o segundo merece
outra revisao.
"""

import re
import time

import herdr
# `so_ascii` e `cortar` moraram aqui ate a tela de terminal precisar dos dois.
# Sao a mesma pergunta — "como este texto cabe na fonte e no buffer da placa" —
# e uma copia divergiria: a tabela de simbolos cresce quando uma TUI nova
# aparece, e crescer em um lugar so e o ponto.
from texto import cortar, so_ascii

# O formato de uma opcao: linha numerada, com o cursor na selecionada. As
# descricoes vem indentadas embaixo e, por nao serem numeradas, ja ficam fora.
#
# TRES cursores porque cada TUI escolheu o seu: `❯` no Claude Code, `›` no
# Codex, `>` no terminal que nao tem os dois na fonte. Sem o do Codex a linha
# da opcao 1 nao casava — e uma lista que comeca no 2 e recusada inteira, por
# nao dar para saber se o 1 rolou para fora da tela. O painel caia na cauda e
# mostrava o comando no lugar da pergunta.
OPCAO_RE = re.compile(r"^[\s│|]*([❯›>])?\s*(\d{1,2})\.\s+(\S.*?)\s*$")

# O rodape que prova que a tela e um SELETOR NAVEGAVEL, e nao uma lista
# numerada qualquer que o agente imprimiu. Sem ele o painel mostra o texto e
# nenhum botao: melhor ficar sem botao do que mandar Enter numa tela que nao
# esperava Enter.
RODAPE_RE = re.compile(r"enter to (select|confirm)", re.I)

# A marca vale LOGO ABAIXO DA LISTA, e nao perto do fim da tela.
#
# Ela era medida do fim (as ultimas seis linhas) ate o dia em que o Claude Code
# passou a desenhar o bloco de tarefas depois do seletor: sete linhas que
# empurravam o rodape para fora da janela. O parse recusava a tela inteira e a
# pergunta chegava ao painel como texto sem botao — justamente nas perguntas de
# brainstorm, que sao as que mais pedem resposta pelo toque.
#
# DEZ linhas porque nas telas reais — Claude Code, Codex, AskUserQuestion — o
# rodape fica a uma ou tres linhas das opcoes, e a ultima delas pode trazer
# descricao indentada antes da marca. Sem limite nenhum, qualquer "enter to
# select" impresso em qualquer ponto abaixo de uma lista numerada do agente
# validaria o formulario.
RODAPE_APOS = 10

REGUA_RE = re.compile(r"^\s*[─━═_-]{10,}\s*$")

# Opcoes que abrem um campo de texto em vez de decidir na hora. O painel precisa
# saber: escolher uma dessas nao resolve o bloqueio, so troca de pergunta.
#
# O nome do agente e curinga (`tell \S+ what to do`): a frase e a mesma no
# Claude Code e no Codex, e so o nome no meio muda. Fixar "claude" deixava a
# opcao do Codex passar por decisao.
TEXTO_RE = re.compile(
    r"type something|chat about this|tell \S+ what to do|tab to edit", re.I)

# Cromo do terminal que nao e conteudo: reguas, spinners, dicas de tecla.
CROMO_RE = re.compile(
    r"^[\s─━═_—│|◔◑◕●\s]+$"
    r"|(?i:esc to cancel)"
    r"|type to queue"
    r"|^\s*[◔◑◕●]\s+(Shell|Bash)"
)

# Tetos, medidos contra a coluna esquerda da segunda tela do painel: ela vai de
# x=20 a x=310, ou seja 290 px, e a fonte base tem 6 px por caractere — 48
# caracteres por linha. 68 bytes ocupam duas linhas de botao com folga.
ROTULO_MAX_B = 68
# A pergunta tem ~4 linhas antes de os botoes comecarem a nao caber.
PERGUNTA_MAX_B = 200


# Caracteres de CAIXA. A tela do agente e um grid: a lista de opcoes pode ter
# uma segunda coluna ao lado (um diff, uma lista de arquivos), e a expressao da
# opcao captura ate o fim da LINHA — entao o vizinho vem colado no rotulo.
#
# Visto na tela em 11/08: "Commit local, sem push  │  camara/session.go".
CAIXA = "│|┃║┌┐└┘├┤┬┴┼╭╮╰╯─━═"
CAIXA_RE = re.compile("[" + re.escape(CAIXA) + "]")

def sem_colunas(texto):
    """Corta no primeiro caractere de caixa: dali para a direita e outra coluna.

    Rotulo de opcao nao tem caixa — nem no Claude Code nem no Codex. Tracinho
    comum tem ("nao-interativo"), e por isso o corte e nos caracteres de
    desenho de caixa e nao em qualquer hifen.
    """
    m = CAIXA_RE.search(texto)
    return (texto[:m.start()] if m else texto).rstrip()


def para_o_painel(texto, limite):
    """Todo texto que sai daqui passa por isto: sem coluna vizinha, sem acento,
    dentro do limite de bytes. Nesta ordem — transliterar depois de cortar por
    byte poderia deixar meio caractere, e cortar antes de transliterar mediria
    bytes que vao deixar de existir."""
    return cortar(so_ascii(sem_colunas(texto)), limite)


def sem_moldura(linha):
    """Tira a caixa: o prompt de permissao vem desenhado dentro de uma."""
    return linha.strip().strip("│|╭╮╰╯├┤┌┐└┘").strip()


def vazia(linhas, i):
    """Linha em branco ou so de moldura/regua — e ela que separa a pergunta do
    comando ou do diff que o agente mostrou acima."""
    return not sem_moldura(linhas[i]) or bool(REGUA_RE.match(linhas[i]))


def paragrafo_ate(linhas, fim):
    """O bloco de texto que TERMINA na linha `fim`, numa linha so.

    Sobe enquanto houver texto, no maximo quatro linhas: acima disso vem o
    comando proposto, que nao cabe na tela do painel.
    """
    fora = []
    i = fim
    while i >= 0 and not vazia(linhas, i) and len(fora) < 4:
        fora.append(sem_moldura(linhas[i]))
        i -= 1
    fora.reverse()
    return " ".join(fora)


# O rotulo com que o Codex explica o pedido. La a pergunta NAO fica colada na
# lista: entre as duas ficam o comando proposto e o "[… N lines]" do corte da
# tela, entao subir do primeiro item traz o script em vez do que se pergunta.
RAZAO_RE = re.compile(r"^Reason:\s*(\S.*)$", re.I)

# Ate onde procurar a pergunta acima da lista. A tela lida tem ~50 linhas, e o
# limite existe so para nao varrer uma tela inteira de log atras de um "?".
ANCORA_LINHAS = 40


def ancora_acima(linhas, desde):
    """Indice da linha de PERGUNTA mais proxima acima de `desde`, ou -1.

    Duas marcas, nesta ordem de encontro: o `Reason:` do Codex e uma linha
    terminada em `?` — o que sobra de universal entre as duas TUIs quando o
    rotulo nao existe.
    """
    for i in range(desde, max(-1, desde - ANCORA_LINHAS), -1):
        linha = sem_moldura(linhas[i])
        if RAZAO_RE.match(linha) or linha.endswith("?"):
            return i
    return -1


def extrair_pergunta(linhas, primeira_opcao):
    """A pergunta que esta na tela, em uma linha.

    O primeiro palpite e o bloco logo acima da lista, que e onde o Claude Code
    a escreve. Nao havendo `?` ali, o bloco provavelmente e outra coisa (o
    comando, o indicador de corte) — e ai vale procurar a pergunta mais acima.
    """
    i = primeira_opcao - 1
    while i >= 0 and vazia(linhas, i):
        i -= 1
    texto = paragrafo_ate(linhas, i)

    if "?" not in texto:
        j = ancora_acima(linhas, i)
        if j >= 0:
            texto = paragrafo_ate(linhas, j)

    # O rotulo do Codex sai: ele e legenda, e a tela do painel cobra por byte.
    m = RAZAO_RE.match(texto)
    return para_o_painel(m.group(1) if m else texto, PERGUNTA_MAX_B)


def parse_form(bruto):
    """Le o formulario de escolha que esta na tela do agente.

    Devolve a pergunta, as opcoes (numero, rotulo, se pedem texto) e em qual
    delas esta o cursor — ou None quando o que esta na tela nao e um formulario
    navegavel.

    `cursor` e o INDICE na lista e nao o numero da opcao: e ele que diz quantas
    setas faltam para chegar no alvo. `None` ali significa "nao da para navegar"
    — as opcoes ainda servem para MOSTRAR, mas nao para responder.
    """
    if not bruto:
        return None
    linhas = bruto.splitlines()

    achados = []
    for i, linha in enumerate(linhas):
        m = OPCAO_RE.match(linha)
        if m:
            achados.append((i, int(m.group(2)), sem_moldura(m.group(3)),
                            m.group(1) is not None))

    # Sobe do ULTIMO item encaixando a numeracao. Assim reguas e linhas em
    # branco no meio da lista nao a quebram, e qualquer lista numerada que o
    # agente tenha escrito acima fica de fora — ela nao encaixa na sequencia.
    bloco = []
    for a in reversed(achados):
        if not bloco or a[1] == bloco[-1][1] - 1:
            bloco.append(a)
        if bloco[-1][1] == 1:
            break
    bloco.reverse()

    # Menos de duas opcoes nao e escolha. E sem o 1 na tela nao da para saber se
    # a lista foi cortada pelo scroll — contar setas a partir de uma lista
    # incompleta erra o alvo, e o alvo errado aqui aprova a coisa errada.
    if len(bloco) < 2 or bloco[0][1] != 1:
        return None

    # A prova de que isto e um seletor navegavel: o rodape vem LOGO ABAIXO da
    # lista. E posicional de proposito — assim o que a TUI desenhe embaixo do
    # conjunto (o bloco de tarefas hoje, outra coisa amanha) deixa de importar,
    # e uma lista numerada solta continua recusada por nao ter rodape nenhum
    # depois dela.
    fim = bloco[-1][0]
    if not any(RODAPE_RE.search(l) for l in linhas[fim + 1:fim + 1 + RODAPE_APOS]):
        return None

    return {
        "pergunta": extrair_pergunta(linhas, bloco[0][0]),
        "opcoes": [{"n": n, "rotulo": para_o_painel(rotulo, ROTULO_MAX_B),
                    "texto": bool(TEXTO_RE.search(rotulo))}
                   for _, n, rotulo, _ in bloco],
        "cursor": next((k for k, a in enumerate(bloco) if a[3]), None),
    }


def parece_seletor(bruto):
    """A tela tem uma opcao numerada COM CURSOR, mesmo sem rodape?

    E o indicio de que `parse_form` recusou uma tela que na verdade e um
    formulario — a lista ja foi desenhada e o rodape ainda nao. Cursor porque
    ele so existe em seletor navegavel: uma lista numerada que o agente
    imprimiu no meio do texto nao tem nenhum.
    """
    if not bruto:
        return False
    for linha in bruto.splitlines():
        m = OPCAO_RE.match(linha)
        if m and m.group(1) is not None:
            return True
    return False


def cauda(bruto, linhas=20):
    """As ultimas linhas uteis da tela, para quando nao ha formulario.

    O painel ainda mostra ALGO: o fim da tela e onde esta a pergunta em texto
    livre. Uma tela vazia com "bloqueado" e menos util do que a frase que o
    agente escreveu.
    """
    if not bruto:
        return ""
    uteis = [l.strip() for l in bruto.splitlines()
             if l.strip() and not CROMO_RE.search(l)]
    # Aqui NAO se corta coluna: sem formulario nao ha lista para separar de um
    # vizinho, e o `│` que sobrar e do proprio texto que se quer mostrar. So a
    # transliteracao, que vale para tudo que chega a placa.
    return cortar(so_ascii("\n".join(uteis[-linhas:])), PERGUNTA_MAX_B)


# ---- O bloqueio corrente, para o /status ----
# Daqui para baixo o modulo LE A TELA de verdade, e nao so texto que alguem lhe
# entregou. A leitura custa um subprocess (~20-40 ms medidos) e o painel pede
# /status a cada 2 s — por isso o cache. Sem ele, cada resposta pagaria uma
# leitura de terminal, e o pior caso (varios paineis) multiplicaria isso.

RELER_S = 5.0        # janela do cache; a pergunta de um agente nao muda rapido

# Tempo para o agente redesenhar depois de uma seta. Abaixo disso a releitura
# pega a tela ANTERIOR e o cursor parece nao ter andado.
ASSENTAR_S = 0.2

_cache = {"pane": None, "chave": None, "form": None, "ts": 0.0, "seq": 0}


def esquecer():
    """Zera o cache. Existe para os testes — nada em producao chama isto."""
    _cache.update({"pane": None, "chave": None, "form": None, "ts": 0.0,
                   "seq": 0})


def atual(agentes, ler=None, agora=None):
    """O formulario do agente bloqueado, pronto para entrar no /status.

    `None` quando ninguem esta bloqueado — e nesse caso NADA e lido: ler custa
    subprocess, e /status responde a cada dois segundos.

    O primeiro bloqueado da lista, que ja vem ordenada. Com dois bloqueados ao
    mesmo tempo o painel mostra um so; `agent_id` diz qual, e o outro continua
    marcado em vermelho no menu da direita.

    `seq` anda quando a PERGUNTA muda, e nao a cada leitura. O firmware desarma
    o botao quando ele anda; um seq que andasse sozinho desarmaria o botao
    debaixo do dedo de quem esta respondendo.
    """
    ler = ler or herdr.ler_pane
    agora = agora or time.time

    alvo = next((a for a in agentes
                 if a.get("state") == "blocked" and a.get("pane_id")), None)
    if not alvo:
        # Esquece o pane, mas NAO o contador: se o mesmo agente bloquear de novo
        # com a mesma pergunta, aquilo e uma pergunta nova para quem olha, e o
        # painel precisa ver `seq` andar para desarmar o botao da rodada
        # anterior.
        _cache.update({"pane": None, "chave": None, "form": None})
        return None

    pane = alvo["pane_id"]
    t = agora()
    # Pane diferente rele NA HORA, sem esperar a janela: mostrar a pergunta de
    # um agente com o nome de outro e pior do que uma leitura a mais.
    if pane != _cache["pane"] or (t - _cache["ts"]) >= RELER_S:
        bruto = ler(pane)
        form = parse_form(bruto)
        # A tela pode ter sido lida NO MEIO DO REDESENHO: a lista ja esta la e o
        # rodape ainda nao, e sem ele nao ha prova de seletor navegavel. Medido
        # em 14/08 na fase de revisao do AskUserQuestion — a tela chegou ao
        # painel como texto, terminando na ultima opcao.
        #
        # Uma releitura resolve, e ela precisa acontecer AQUI: o cache serviria
        # o quadro parcial por RELER_S segundos, e cinco segundos sem botao numa
        # tela que tem opcoes e o defeito inteiro, so que mais demorado.
        #
        # So paga quando ha cursor na tela (ver `parece_seletor`): texto corrido
        # nao vira formulario por reler.
        if not form and parece_seletor(bruto):
            time.sleep(ASSENTAR_S)
            bruto2 = ler(pane)
            form2 = parse_form(bruto2)
            if form2:
                bruto, form = bruto2, form2
        _cache["pane"] = pane
        _cache["ts"] = t
        # Sem formulario vai a cauda da tela e nenhuma opcao. Sem conseguir ler
        # vai vazio — e o painel ainda anuncia o bloqueio, porque sumir
        # esconderia o unico estado que exige acao de quem olha.
        _cache["form"] = ({"pergunta": form["pergunta"], "opcoes": form["opcoes"]}
                          if form else {"pergunta": cauda(bruto), "opcoes": []})

    dados = _cache["form"]
    chave = (pane, dados["pergunta"],
             tuple((o["n"], o["rotulo"]) for o in dados["opcoes"]))
    if chave != _cache["chave"]:
        _cache["chave"] = chave
        _cache["seq"] += 1

    return {"agent_id": alvo.get("session_id"), "pane_id": pane,
            "pergunta": dados["pergunta"], "opcoes": dados["opcoes"],
            "seq": _cache["seq"]}


# ---- Responder ----
# Este e o unico ponto do projeto que aperta Enter num prompt de permissao. Tudo
# aqui existe para que ele nunca aperte no lugar errado.
def responder(pane_id, n, rotulo, ler=None, teclas=None):
    """Move o cursor ate a opcao `n` e so entao confirma. False em qualquer duvida.

    Mandar o texto da opcao nao escolhe nada: num seletor as letras sao
    ignoradas e o Enter confirma o que estiver marcado — sempre a primeira. Daí
    navegar por setas.

    O `rotulo` vem de quem toca, e nao daqui: e o que estava ESCRITO no botao na
    hora do toque. Se a tela mudou no caminho, ele nao bate e nada e enviado. Sem
    isso, um toque de dois segundos atras aprovaria a pergunta seguinte.

    E rele DEPOIS de navegar. Se o cursor nao parou onde devia — agente ocupado,
    tela redesenhada no meio — aborta antes do Enter. Confirmar ali aprovaria a
    opcao errada, que e o pior desfecho possivel desta funcao.
    """
    ler = ler or herdr.ler_pane
    teclas = teclas or herdr.enviar_teclas

    form = parse_form(ler(pane_id))
    if not form or form["cursor"] is None:
        return False
    alvo = next((k for k, o in enumerate(form["opcoes"]) if o["n"] == n), None)
    if alvo is None or form["opcoes"][alvo]["rotulo"] != rotulo:
        return False

    delta = alvo - form["cursor"]
    if delta:
        if not teclas(pane_id, ["Down" if delta > 0 else "Up"] * abs(delta)):
            return False
        time.sleep(ASSENTAR_S)
        form = parse_form(ler(pane_id))
        if (not form or form["cursor"] != alvo or alvo >= len(form["opcoes"])
                or form["opcoes"][alvo]["n"] != n):
            return False

    return teclas(pane_id, ["Enter"])
