#!/usr/bin/env python3
"""Le o estado dos agentes do herdr, o multiplexador de terminal.

POR QUE ISTO EXISTE
O painel so enxergava o que o Claude Code publica, e o estado dependia de um
hook disparar. O herdr le a TELA RENDERIZADA e reconhece 19 agentes diferentes,
entao ele resolve os dois limites de uma vez: ve codex, gemini e cursor, e nao
tem furo quando um hook nao dispara.

Medido ao vivo: com tres sessoes abertas, o herdr concordou com a nossa API nos
tres estados — depois dos consertos de 31/07. Antes deles, ele ja sabia a
resposta certa de uma sessao que a nossa API errava havia dez minutos.

POR QUE A CLI E NAO O SOCKET
O socket e NDJSON, e no Windows um named pipe cujo nome e o caminho inteiro
(`\\\\.\\pipe\\C:\\Users\\...\\herdr.sock`). Nao ha caminho stdlib para ele:
`open()` normaliza o caminho e tropeca no `C:` do meio, e `CreateFileW` devolve
erro 123 (nome invalido). A CLI devolve JSON, e a documentacao do proprio herdr
manda usar ela para automacao. Medido: mediana de 19 ms por chamada.
"""

import json
import os
import shutil
import subprocess
import threading
import time

CONSULTA_S = 2.0          # mesmo ritmo com que o ESP consulta /status
TIMEOUT_S = 2.0           # nunca esperar mais de um ciclo

# Quao velho um retrato pode ficar antes de `snapshot()` deixar de confiar
# nele. 3x CONSULTA_S e nao 1x: da folga para UMA consulta lenta (perto do
# proprio TIMEOUT_S) sem que o retrato pisque online/offline a toa, mas sem
# deixar dado morto passar por vivo por muito tempo. Existe por causa de
# `_laco`: se a thread morrer depois de uma consulta boa, `_retrato["ts"]` para
# de andar e, sem esta guarda, `online` ficaria True para sempre — e `fundir`
# passaria a sobrepor a nossa maquina de estados com um retrato imovel (uma
# sessao que virasse `blocked` de verdade mostraria `idle` indefinidamente).
RETRATO_MAX_IDADE_S = 3 * CONSULTA_S

# Sem isto, cada consulta pisca uma janela de console — a API roda sob
# pythonw.exe, sem console proprio, e um flash a cada 2 segundos seria
# insuportavel. Zero em qualquer plataforma que nao seja Windows.
SEM_JANELA = 0x08000000 if os.name == "nt" else 0

_retrato = {"online": False, "agents": [], "ts": 0.0}
_lock = threading.Lock()

# Instalacao padrao no Windows. Só e consultada depois do PATH.
CAMINHOS_PADRAO = (
    os.path.join(os.environ.get("LOCALAPPDATA", ""),
                 "Programs", "Herdr", "bin", "herdr.exe"),
    "/usr/local/bin/herdr",
    os.path.expanduser("~/.local/bin/herdr"),
)


def achar_binario():
    """O executavel do herdr, ou None quando ele nao esta instalado.

    None nao e erro: a API funciona exatamente como antes sem o herdr.
    """
    p = os.environ.get("HERDR_BIN_PATH")
    if p and os.path.isfile(p):
        return p
    p = shutil.which("herdr")
    if p:
        return p
    for c in CAMINHOS_PADRAO:
        if c and os.path.isfile(c):
            return c
    return None


def repo_de(cwd):
    """Ultimo componente do caminho, nos dois separadores.

    O herdr devolve o cwd do pane; a nossa statusline devolve o nome do repo. Um
    `basename` aproxima os dois o bastante para a mesma coluna da tela.
    """
    if not cwd:
        return ""
    s = str(cwd).replace("\\", "/").rstrip("/")
    return s.rsplit("/", 1)[-1] if "/" in s else s


def parse_agentes(saida):
    """Le a saida de `herdr agent list`.

    Na duvida devolve lista vazia. Uma CLI que mudou de formato tem que
    silenciar este modulo, nunca derrubar o painel.
    """
    try:
        d = json.loads(saida)
    except (ValueError, TypeError):
        return []
    if not isinstance(d, dict):
        return []
    resultado = d.get("result")
    if not isinstance(resultado, dict):
        # "result" pode vir ausente, ou truthy mas do tipo errado (lista,
        # string, bool, numero) — nenhum desses tem `.get`.
        resultado = {}
    agentes = resultado.get("agents")
    if not isinstance(agentes, list):
        return []

    fora = []
    for a in agentes:
        if not isinstance(a, dict):
            continue
        pane = a.get("pane_id")
        estado = a.get("agent_status")
        # Sem pane_id nao ha como casar com as nossas sessoes; sem estado nao ha
        # o que dizer. Os dois sao obrigatorios.
        if not isinstance(pane, str) or not isinstance(estado, str):
            continue
        cwd = a.get("cwd") or ""
        fora.append({
            "pane_id": pane,
            "state": estado,
            "agent": a.get("agent") or "",
            "cwd": cwd,
            "repo": repo_de(cwd),
            "focused": bool(a.get("focused")),
            "name": a.get("name") or "",
        })
    return fora


def consultar(binario):
    """Uma consulta. Devolve a lista, ou None quando nao deu para saber.

    None e [] sao coisas diferentes de proposito: [] significa "o herdr esta no
    ar e nao ha agente nenhum", e None significa "nao consegui perguntar".
    Tratar os dois igual esvaziaria o painel a cada piscada do servidor.
    """
    try:
        # encoding explicito: sem ele o subprocess decodifica pelo locale
        # (cp1252 no Windows) e um caractere fora dessa tabela — acento no nome
        # de um pane, por exemplo — estoura dentro da thread leitora e volta
        # como stdout vazio. Medido em `ler_pane`, onde a tela do terminal traz
        # caixa e seta o tempo todo; aqui e menos provavel porque o JSON escapa
        # nao-ASCII, mas o custo de blindar e uma linha.
        r = subprocess.run([binario, "agent", "list"],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace",
                           timeout=TIMEOUT_S, creationflags=SEM_JANELA)
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    if r.returncode != 0:
        return None
    return parse_agentes(r.stdout)


def _aplicar(agentes, ao_consultar=None):
    """Guarda o resultado de uma consulta. `None` marca offline mas PRESERVA a
    ultima lista conhecida: durante uma falha passageira, dado velho e melhor do
    que tela vazia, e `online` diz que ele e velho.

    `ao_consultar` recebe o `online` resultante desta consulta. Ele existe para
    que este modulo nao precise conhecer quem depende dele — a API liga os dois.
    """
    with _lock:
        if agentes is None:
            _retrato["online"] = False
        else:
            _retrato["online"] = True
            _retrato["agents"] = agentes
            _retrato["ts"] = time.time()

    # Avisa DEPOIS de gravar, e com o mesmo `online` que snapshot() devolveria:
    # quem escuta tem que ver a mesma verdade que quem le, guarda de frescor
    # inclusa — um calculo em paralelo nao teria essa guarda.
    if ao_consultar:
        try:
            ao_consultar(snapshot()["online"])
        except Exception:
            pass    # o sensor nunca morre por causa de quem ele avisa


def snapshot():
    """Retrato corrente. Copia rasa: quem le nao mexe no que a thread escreve.

    `online` some (vira False) quando o retrato esta mais velho que
    RETRATO_MAX_IDADE_S, mesmo que a ultima consulta guardada tenha tido
    sucesso — ver o comentario da constante. Sem esta guarda, uma thread
    `_laco` morta (ou so lenta) congelaria `online: True` com uma lista cada
    vez mais velha, e `fundir` sobreporia essa foto parada por cima da nossa
    propria maquina de estados.
    """
    with _lock:
        online = _retrato["online"]
        if online and (time.time() - _retrato["ts"]) > RETRATO_MAX_IDADE_S:
            online = False
        return {"online": online,
                "agents": list(_retrato["agents"]),
                "ts": _retrato["ts"]}


def _laco(binario, ao_consultar=None):
    while True:
        try:
            _aplicar(consultar(binario), ao_consultar=ao_consultar)
        except Exception:
            # O sensor e um extra: um erro nao previsto aqui (e nao coberto
            # pelo try/except de `consultar`, que ja trata os casos normais de
            # subprocess) nao pode matar a thread. Uma thread morta congelava
            # `_retrato` para sempre antes da guarda de frescor em snapshot();
            # com ela, o pior caso agora e `online` virar False depois de
            # RETRATO_MAX_IDADE_S — nunca um retrato imovel eternamente vivo.
            pass
        time.sleep(CONSULTA_S)


_binario_ativo = None   # caminho do binario em uso, para status()/health


def start(ao_consultar=None):
    """Liga o sensor. Sem herdr instalado, nao faz nada e nao reclama.

    `ao_consultar`, se dado, e chamado a cada ciclo com o `online` daquela
    consulta — repassado ate `_laco`/`_aplicar`, que e quem de fato o invoca.

    Devolve o caminho do binario (util para o log de boot) ou None.
    """
    global _binario_ativo
    binario = achar_binario()
    if not binario:
        return None
    _binario_ativo = binario
    t = threading.Thread(target=_laco, args=(binario, ao_consultar), daemon=True)
    t.start()
    return binario


def status():
    """Estado do sensor, para diagnostico via /health. Nunca levanta.

    Separado de `snapshot()` porque serve outra pergunta: nao "quais agentes",
    e sim "o sensor esta funcionando" — achou o binario? o retrato esta
    online? ha quanto tempo veio a ultima consulta boa?
    """
    r = snapshot()
    idade = (time.time() - r["ts"]) if r["ts"] else None
    return {
        "binario": _binario_ativo,
        "online": r["online"],
        "retrato_idade_s": round(idade, 1) if idade is not None else None,
    }


# ---- Escrever no terminal ----
# Ate aqui este modulo so LIA. Daqui para baixo ele age, e a diferenca de risco
# e categorica: o pior defeito de um leitor e mentir, o de um escritor e agir
# sem que voce queira. Por isso quem chama NAO passa texto livre — ver a lista
# branca em claude_metrics_api.py. Aqui embaixo o texto ja chegou aprovado.
def enviar(pane_id, texto):
    """Escreve `texto` no pane e manda Enter, de forma atomica.

    Devolve True quando o herdr aceitou. False em qualquer duvida — e falso NAO
    significa que nada foi escrito: se o comando estourou o timeout depois de
    ter escrito, o efeito aconteceu e nos nao soubemos. Quem chama tem que
    tratar isto como "nao sei", nunca como "nao fez".
    """
    binario = _binario_ativo or achar_binario()
    if not binario or not pane_id or not texto:
        return False
    try:
        r = subprocess.run([binario, "pane", "run", pane_id, texto],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace",
                           timeout=TIMEOUT_S, creationflags=SEM_JANELA)
    except (OSError, ValueError, subprocess.SubprocessError):
        return False
    return r.returncode == 0


# Quantas linhas da tela pedir. O formulario de aprovacao cabe folgado em 50, e
# pedir mais so aumenta o texto que o parser tem que varrer a cada leitura.
LER_LINHAS = 50

# As UNICAS teclas que este modulo manda. A lista branca mora aqui, no modulo
# que age, e nao em quem chama — e a mesma razao da lista branca de comandos em
# claude_metrics_api.py: a API escuta em 0.0.0.0 sem autenticacao, e tecla
# arbitraria vinda da rede seria um teclado apontado para os seus agentes.
#
# Tres bastam para responder um seletor: subir, descer, confirmar. `Escape` e
# `C-c` ficam de fora de proposito — cancelar o turno de um agente pela rede nao
# e uma acao que o painel precise oferecer, e o custo de um toque errado nela e
# alto demais.
TECLAS_OK = ("Up", "Down", "Enter")


def ler_pane(pane_id, linhas=LER_LINHAS, source="recent"):
    """A tela de um pane, em texto cru. String vazia quando nao deu para ler.

    Vazio significa "nao consegui", e quem chama trata como "sem formulario" —
    que e o comportamento seguro: o painel mostra o bloqueio sem botao, em vez
    de mostrar um botao que talvez nao corresponda ao que esta na tela.

    `source` default "recent" porque quem le formulario precisa de historico: a
    pergunta pode ter rolado para fora da area visivel. A tela de terminal pede
    "visible", que e o viewport — e o viewport e o que a trava de resolucao
    reflowou e o que a rolagem nativa move.

    `--format text` porque o parser casa expressao regular, e os codigos de cor
    no meio das linhas quebrariam o match.
    """
    binario = _binario_ativo or achar_binario()
    if not binario or not pane_id:
        return ""
    try:
        r = subprocess.run([binario, "pane", "read", pane_id,
                            "--source", source, "--lines", str(linhas),
                            "--format", "text"],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace",
                           timeout=TIMEOUT_S, creationflags=SEM_JANELA)
    except (OSError, ValueError, subprocess.SubprocessError):
        return ""
    # `or ""` e nao `r.stdout` direto: com decodificacao falha o subprocess
    # devolve None aqui, e o None viajaria ate estourar longe deste modulo.
    return (r.stdout or "") if r.returncode == 0 else ""


def enviar_teclas(pane_id, teclas):
    """Manda teclas da lista branca para o pane. False em qualquer duvida.

    TODAS numa chamada so, e nao uma por tecla: entre duas chamadas o agente
    pode redesenhar a tela, e aí as setas seguintes passariam a contar a partir
    de outro estado — parando o cursor em outra opcao.

    Como em `enviar`, False NAO significa que nada foi enviado: um timeout
    depois de o herdr ter aceitado deixa o efeito feito e nos sem saber.
    """
    binario = _binario_ativo or achar_binario()
    if not binario or not pane_id or not teclas:
        return False
    if not all(t in TECLAS_OK for t in teclas):
        return False
    try:
        r = subprocess.run([binario, "pane", "send-keys", pane_id, *teclas],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace",
                           timeout=TIMEOUT_S, creationflags=SEM_JANELA)
    except (OSError, ValueError, subprocess.SubprocessError):
        return False
    return r.returncode == 0
