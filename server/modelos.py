#!/usr/bin/env python3
"""O modelo em uso de um agente que nao e o Claude Code.

POR QUE ISTO EXISTE
Um agente do Codex ou do Antigravity chega ao painel pelo herdr, que ve a tela
e sabe QUAL ferramenta e — mas nao qual modelo. O card desenhava "-". As duas
CLIs gravam o modelo em disco a cada turno; este modulo vai buscar.

COMO ELE CASA UM PANE COM UM ARQUIVO
Pelo `cwd` do pane mais a recencia do arquivo. E heuristico e assumido como
tal: dois Codex no MESMO diretorio desempatam por mtime, e a tela pode trocar
de modelo por alguns segundos. Um hook dentro de cada CLI daria pareamento
exato pelo HERDR_PANE_ID; foi descartado pelo custo de manter duas pecas
instaladas fora do repo, que o proprio herdr sobrescreve quando reinstala a
integracao dele.

REGRA DE OURO
Como em planos.py: nada levanta. Formato novo vira None e a tela mostra "—".
"""

import glob
import json
import os
import re
import sqlite3
import threading
import time

# Sufixo de esforco de raciocinio, colado no fim do identificador por algumas
# CLIs. Sai do nome porque o esforco tem chip proprio na tela, e repeti-lo aqui
# so roubaria largura do que importa.
ESFORCOS = ("high", "medium", "low", "thinking", "minimal")

# Marcas cuja grafia de tela nao e a capitalizacao do identificador. E tambem a
# lista das que COLAM na versao: "GPT-5.6", contra "Gemini 3.7" por extenso.
GRAFIAS = {"gpt": "GPT"}


def bonito(model_id):
    """"gpt-5.6-terra" -> "GPT-5.6 Terra". String vazia quando nao ha nome."""
    if not isinstance(model_id, str) or not model_id:
        return ""

    # Filtrar ANTES do teste de esforco, nao depois. Um hifen solto (extremidade
    # ou duplo) deixa `partes[-1]` vazio, o teste de esforco falha em silencio,
    # e o "-high" sobrevive ate a tela duplicando o chip que ESFORCOS existe
    # para nao repetir. Filtrar so na hora de montar `fora` tira o espaco em
    # branco mas deixa esse bug de pe — a ordem das duas linhas e o conserto.
    partes = [p for p in model_id.split("-") if p]
    if len(partes) > 1 and partes[-1].lower() in ESFORCOS:
        partes = partes[:-1]

    # Numeros separados por hifen sao UMA versao: "4-6" e 4.6. Junta-los antes
    # de capitalizar evita "Claude Opus 4 6".
    juntas = []
    for p in partes:
        if juntas and p.isdigit() and juntas[-1].replace(".", "").isdigit():
            juntas[-1] += "." + p
        else:
            juntas.append(p)

    # "-", "--" ou so o sufixo de esforco esvaziam `partes` (e por tabela,
    # `juntas`). Sem esta guarda o `fora[0]` la embaixo estoura IndexError.
    if not juntas:
        return ""

    fora = []
    for p in juntas:
        low = p.lower()
        if low in GRAFIAS:
            fora.append(GRAFIAS[low])
        elif p[0].isdigit():
            fora.append(p)  # "70B" nao pode virar "70b": capitalize() minusculiza o resto
        else:
            fora.append(p.capitalize())

    # A sigla cola na versao ("GPT-5.6"); o nome por extenso separa por espaco
    # ("Gemini 3.7"). E como as proprias CLIs escrevem na tela delas, e a
    # diferenca e exatamente a de sigla contra palavra — por isso a condicao e
    # "esta em GRAFIAS", e nao "o proximo pedaco e um numero".
    if len(fora) >= 2 and juntas[0].lower() in GRAFIAS:
        cabeca = f"{fora[0]}-{fora[1]}"
        resto = fora[2:]
    else:
        cabeca = fora[0]
        resto = fora[1:]
    return " ".join([cabeca] + resto)


RAIZ_CODEX = os.path.expanduser("~/.codex")
RAIZ_GEMINI = os.path.expanduser("~/.gemini")

# Quantos arquivos de sessao olhar, do mais novo para o mais velho. O Codex
# guarda meses de historico; varrer tudo a cada consulta seria caro e inutil —
# um pane VIVO escreveu ha segundos.
MAX_ROLLOUTS = 40

# Quantas linhas da cauda VARRER atras do ultimo `turn_context`. O modelo
# aparece em toda volta, entao o fim basta. Medido nos 130 rollouts desta
# maquina: o `turn_context` mais distante do fim estava a 191 linhas, e 400 e o
# dobro disso.
#
# Duas contas e nao uma (esta e CAUDA_BYTES) porque linha de rollout tem
# tamanho selvagem: um arquivo gastou 728 KB em 24 linhas, outro 428 KB em 191.
# Um teto so, em qualquer das duas unidades, erraria o outro caso.
CAUDA_LINHAS = 400

# Quantos bytes do FIM trazer para a memoria. Existe porque `readlines()` lia o
# arquivo INTEIRO: no maior rollout daqui (74 MB) sao 242 ms e 78 MB de pico,
# contra 11 ms e 8 MB por esta janela — dentro de um endpoint que responde em
# 2-14 ms. A janela dos 40 mais recentes soma 9,6 MB hoje e escapava por sorte;
# uma maratona de Codex poe um arquivo de 74 MB no topo da lista.
#
# 2 MiB da quase tres vezes a folga do pior caso medido (728 KB do fim ate o
# ultimo `turn_context`, nos 130 rollouts desta maquina).
CAUDA_BYTES = 2 * 1024 * 1024

# Por quanto tempo a lista de arquivos ja ordenada continua valendo. A API
# consulta uma vez por PANE dentro do mesmo ciclo, e sem isto a varredura do
# diretorio (glob mais ordenacao por mtime: 21 ms para os 130 rollouts daqui)
# se repetia inteira a cada pane — 8 panes de Codex custavam 397 ms, e passaram
# a custar 71 ms.
#
# Cinco segundos e folgado para o ciclo inteiro de panes e curto o bastante
# para que o ciclo seguinte (30 s, pelo cache da API) SEMPRE varra de novo — na
# pratica o cache nunca serve lista velha entre ciclos, so dentro de um. O
# atraso maximo para uma sessao recem-CRIADA aparecer e o proprio TTL; turno
# novo em sessao que ja existe aparece na hora, porque so a LISTA e memorizada,
# nunca o conteudo dos arquivos.
CACHE_TTL_S = 5.0

# O teto de idade da RESERVA — e so dela. Leia o paragrafo inteiro antes de
# "consertar o esquecimento" aplicando-o tambem ao casamento por `cwd`.
#
# O Antigravity so escreve no banco quando um turno termina, entao um agente
# recem-aberto ainda nao tem banco proprio. Sem teto, a reserva entregava a
# conversa de ONTEM com cara de atual (medido: o banco mais novo desta maquina
# tinha 27,7 h e respondia assim mesmo). Na reserva a idade e a UNICA defesa
# contra mostrar conversa alheia, porque ali nao ha nada ligando o banco ao
# pane — so a ordem de recencia.
#
# No casamento por `cwd` o teto seria dano puro. Quem poe um agente na lista do
# painel e o herdr ve-lo VIVO na tela: se o pane esta vivo em D:\workspace\gcloud
# e o banco daquele projeto diz `claude-opus-4-6-thinking`, esse E o modelo
# selecionado — a pessoa so nao mandou nada desde ontem. E o caso ruim que este
# teto existe para pegar nem chega ali: um Antigravity novo no projeto X nao
# casa com o banco de ontem do projeto Y, e so alcancava o modelo errado pela
# reserva.
#
# Doze horas cobrem um dia de trabalho inteiro, para a reserva nao jogar fora
# uma conversa aberta de manha e ainda na tela a noite; alem disso o banco e
# necessariamente de outro dia, e "—" e mais honesto do que um modelo errado
# com cara de certo.
MAX_IDADE_S = 12 * 3600

# Quantos bancos abrir, do mais novo para o mais velho. O teto de idade nao
# serve mais de limite (ele agora so decide a reserva), e sem um corte proprio a
# busca pelo `cwd` varreria todo o historico do Antigravity. Mesma razao do
# MAX_ROLLOUTS: a conversa que esta numa tela AGORA nao e a 41a mais velha.
#
# 40 e generoso de proposito. Nesta maquina os 8 projetos com conversa real
# casam entre as posicoes 0 e 12 de 33 — um corte apertado (o `[:3]` de antes e
# o exemplo) troca custo por RESPOSTA ERRADA, e foi disso que este modulo
# acabou de sair. Quem casa cedo sai cedo: 4 ms para o banco no topo, 12 ms para
# o de posicao 12. Quem paga a lista toda e so o pane que nao casa com nada —
# 57,7 ms para os 33 bancos daqui. Ver a preocupacao do custo em `_metadados`.
MAX_BANCOS = 40

_cache = {}                      # padrao de glob -> (instante, [(caminho, mtime)])
_cache_lock = threading.Lock()


def _agora():
    """O relogio do modulo, num ponto so para o teste poder mover o tempo.

    Relogio de PAREDE e nao `monotonic` porque o mesmo relogio serve ao teto de
    idade do Antigravity, que compara com `mtime` de arquivo — e mtime e de
    parede. Um relogio so significa um ponto de troca so no teste, e teste que
    espera relogio de parede passar e teste lento e intermitente. O risco de
    nao usar `monotonic` no TTL (um acerto de relogio para tras servir lista
    velha por ate CACHE_TTL_S a mais) e irrelevante numa janela de 5 s.
    """
    return time.time()


def _esquecer():
    """Apaga o cache de varredura. Existe para o teste comecar cada caso do zero."""
    with _cache_lock:
        _cache.clear()


def _por_recencia(padrao, recursive=False):
    """Os arquivos que casam `padrao`, do mais novo para o mais velho.

    Pares `(caminho, mtime)`: o teto de idade do Antigravity precisa do mtime, e
    refaze-lo depois seria pagar a varredura duas vezes. Lista vazia quando o
    diretorio nao existe — quem chama trata como "nenhum arquivo", que e o
    mesmo desfecho.

    Memorizado por padrao (que ja carrega a raiz) com TTL curto: ver
    CACHE_TTL_S. E o que faz a varredura acontecer uma vez por ciclo em vez de
    uma vez por pane, sem que quem chama precise saber que existe um ciclo.
    """
    agora = _agora()
    with _cache_lock:
        guardado = _cache.get(padrao)
        if guardado is not None and 0 <= (agora - guardado[0]) < CACHE_TTL_S:
            return guardado[1]

    try:
        achados = glob.glob(padrao, recursive=recursive)
    except OSError:
        achados = []

    pares = []
    for f in achados:
        try:
            pares.append((f, os.path.getmtime(f)))
        except OSError:
            # O arquivo sumiu ENTRE o glob e o stat: o Codex rotaciona sessao
            # enquanto o painel varre. Aqui um sumico pula UM arquivo; com o
            # `arquivos.sort(key=os.path.getmtime)` de antes, o mesmo sumico
            # derrubava a sonda inteira para None.
            continue
    pares.sort(key=lambda p: p[1], reverse=True)

    with _cache_lock:
        _cache[padrao] = (agora, pares)
    return pares


def _dict(v):
    """`v` quando for dicionario, senao um vazio.

    Nao e `v or {}`: esse idioma so protege contra None e falsy. Um campo
    truthy mas do TIPO errado — lista, string, bool, numero — passa direto e
    estoura no `.get` seguinte com AttributeError, e este modulo prometeu la em
    cima que nada levanta. O herdr ja tinha tropecado nisto em `parse_agentes`,
    e o comentario de la esta escrito pelo mesmo motivo.
    """
    return v if isinstance(v, dict) else {}


def _norm(p):
    """Um caminho na forma que da para comparar: separador, caixa e barra final.

    O `cwd` que o herdr da vem do pane, e o que a CLI gravou vem do processo
    dela: os dois divergem em caixa e separador o tempo todo no Windows. O blob
    do Antigravity grava com "/" e o herdr entrega com "\\".
    """
    return str(p or "").replace("\\", "/").rstrip("/").lower()


def _mesma_pasta(a, b):
    """Dois caminhos apontam para o mesmo diretorio? Vazio nao casa com nada."""
    return bool(_norm(a)) and _norm(a) == _norm(b)


def _cauda(fh):
    """As ultimas linhas de um arquivo aberto em BINARIO, sem ler o meio."""
    tam = fh.seek(0, os.SEEK_END)
    inicio = max(0, tam - CAUDA_BYTES)
    fh.seek(inicio)
    linhas = fh.read().decode("utf-8", "replace").split("\n")
    if inicio:
        # Entrar pelo MEIO do arquivo faz o primeiro pedaco do buffer ser o
        # rabo de uma linha que comecou antes dele: JSON pela metade, e o
        # primeiro byte pode ainda ser o meio de um caractere UTF-8. Fora.
        linhas = linhas[1:]
    return linhas[-CAUDA_LINHAS:]


def modelo_codex(cwd, raiz=None):
    """O modelo do Codex que roda em `cwd`. None quando nao da para saber."""
    base = str(raiz or RAIZ_CODEX)
    padrao = os.path.join(base, "sessions", "**", "*.jsonl")

    for f, _mtime in _por_recencia(padrao, recursive=True)[:MAX_ROLLOUTS]:
        try:
            with open(f, "rb") as fh:
                # `readline` COM teto: um arquivo sem quebra de linha nenhuma
                # faria a leitura da "primeira linha" carregar o arquivo
                # inteiro pela porta dos fundos — o mesmo custo que CAUDA_BYTES
                # existe para evitar. Linha truncada nao vira JSON e o arquivo
                # e pulado, que e o desfecho certo: nao da para saber o cwd.
                meta = json.loads(fh.readline(CAUDA_BYTES)
                                  .decode("utf-8", "replace"))
                if not isinstance(meta, dict):
                    continue
                if not _mesma_pasta(cwd, _dict(meta.get("payload")).get("cwd")):
                    continue
                linhas = _cauda(fh)
        except (OSError, ValueError, TypeError):
            continue

        # De tras para frente: o ultimo turn_context e o que esta valendo.
        for l in reversed(linhas):
            try:
                d = json.loads(l)
            except (ValueError, TypeError):
                continue
            if not isinstance(d, dict) or d.get("type") != "turn_context":
                continue
            m = _dict(d.get("payload")).get("model")
            if isinstance(m, str) and m:
                return bonito(m)
        # `return` e nao `continue`: uma vez que um arquivo casou o cwd, este
        # modulo se compromete com ele. Cair para o proximo seria mostrar o
        # modelo de OUTRA sessao do mesmo diretorio — mais velha, e por isso
        # justamente a que nao esta na tela.
        return None
    return None


# O identificador do modelo dentro do blob de `executor_metadata`. O blob e
# binario e o formato interno nao e documentado; a expressao acha o nome sem
# depender da serializacao em volta dele.
RE_MODELO = re.compile(rb"(gemini|claude|gpt)-[a-z0-9.\-]{2,40}")

# Bytes que, logo depois de um caminho, significam que ele NAO acabou ali. Sao
# os que podem fazer parte de um nome de pasta; qualquer outro (byte de
# protocolo, aspas, controle) e fim de caminho. Ver `_blob_cita_pasta`.
#
# Nao ha caixa alta porque o palheiro chega em minusculas. O espaco esta na
# lista de proposito: existe pasta com espaco no nome nesta maquina
# ("D:/workspace intelliJ/sol"), e sem ele "D:/workspace" casaria dentro dela.
_CONTINUA_CAMINHO = b"abcdefghijklmnopqrstuvwxyz0123456789/._- "


def _modelo_do_blob(blob):
    """O identificador do modelo dentro do blob, ou None.

    O ULTIMO casamento, nunca o primeiro. Antes do modelo o blob traz a
    allowlist de ferramentas que o USUARIO configurou — trecho literal do banco
    real desta maquina:

        command(powershell) command(claude) command(gemini) command(gh)
        command(copilot) ... $mcp(chrome_devtools/evaluate_script)
        command(*) unsandboxed(*) ... @ claude-opus-4-6-thinking

    Nenhuma dessas entradas tem hifen depois da palavra do fornecedor, entao
    `search()` acertava por sorte. Um MCP chamado `claude-flow` (nome real e
    popular, como `gemini-cli` e `gpt-researcher`) casa antes do modelo e faz a
    sonda devolver "Claude Flow" no lugar dele. O modelo vem DEPOIS da
    allowlist; por isso o ultimo. Nao troque de volta por `search()`.
    """
    ultimo = None
    for m in RE_MODELO.finditer(blob):
        ultimo = m.group(0)
    return ultimo.decode("ascii", "replace") if ultimo else None


def _blob_cita_pasta(blob, cwd):
    """O blob cita `cwd` como diretorio do projeto?

    Procura a AGULHA no palheiro, e nao o contrario. Extrair "o caminho" do
    blob seria ter de escolher entre varios: o mesmo blob traz o diretorio de
    skills do proprio Antigravity (`C:\\Users\\...\\.gemini\\skills`), o
    caminho do projeto (`D:/workspace/gcloud`) e uma copia dele em URI
    (`file:///D:/workspace/gcloud`, com `%20` no lugar do espaco). Pior: o
    caminho nao termina em delimitador nenhum — ele vem com o tamanho na
    frente, e logo atras dele ja comeca o campo seguinte, com um byte arbitrario
    (`\\xda` e `\\x98` nos 24 bancos daqui que trazem caminho). Toda regra de
    "ate onde vai a string" erra em algum banco.

    Perguntar "este cwd aparece?" dispensa saber onde o caminho acaba: a
    resposta e sim ou nao, e as copias espurias nunca respondem sim por engano —
    a copia em URI cita o MESMO diretorio, e os caminhos internos do Antigravity
    nunca sao um cwd de pane.

    A guarda de fronteira e obrigatoria: sem ela `D:/workspace` casaria DENTRO
    de `D:/workspace/gcloud`, e as duas pastas tem conversa propria nesta
    maquina. Ela custa um caso: byte alto conta como fim de caminho (e o que ele
    e nos bancos reais), entao uma pasta irma com acento — `D:/x` contra
    `D:/xé` — casaria por engano. Um byte so nao separa "etiqueta do proximo
    campo" de "primeiro pedaco de caractere acentuado", e o caso medido pesa
    mais que o imaginado.
    """
    agulha = _norm(cwd).encode("utf-8", "replace")
    if not agulha:
        return False
    palheiro = blob.replace(b"\\", b"/").lower()

    i = palheiro.find(agulha)
    while i != -1:
        prox = palheiro[i + len(agulha):i + len(agulha) + 1]
        if not prox or prox not in _CONTINUA_CAMINHO:
            return True     # fim do palheiro ou fim do caminho: casou de verdade
        i = palheiro.find(agulha, i + 1)
    return False


def _metadados(banco):
    """O blob de `executor_metadata` de maior `idx`. None em qualquer duvida.

    NAO e memorizado, e este e o custo conhecido da sonda: o resultado depende
    so de (arquivo, mtime), nunca do `cwd`, entao N panes que nao casam com nada
    releem os mesmos bancos N vezes — 57,7 ms por pane com os 33 daqui, 531 ms
    com 8 panes. O caso realista (cada pane casando o seu projeto) sai cedo do
    laco e custa 83 ms para os mesmos 8. Memorizar por (caminho, mtime) com o
    TTL de CACHE_TTL_S resolve, e e o mesmo remedio que `_por_recencia` aplicou
    a varredura; nao esta feito porque o custo ainda nao foi visto na
    integracao, e o painel consulta uma vez a cada 30 s.
    """
    try:
        # mode=ro: o arquivo pertence a um processo VIVO, e o painel nao pode
        # brigar com ele por lock. timeout curto pelo mesmo motivo — esperar um
        # banco ocupado travaria a thread de consulta.
        #
        # `limit 1`, e nao 20: medido nos 28 bancos com metadados desta
        # maquina, a linha de maior `idx` SEMPRE trouxe o modelo, e trouxe o
        # caminho do projeto em todos os que tinham um. As outras 19 nunca
        # foram usadas por ninguem — carregar 20 blobs para ler o primeiro era
        # peso morto, e o `for` que percorria a lista era complexidade que
        # nenhum banco real exercitou.
        con = sqlite3.connect(f"file:{banco}?mode=ro", uri=True, timeout=0.5)
        try:
            linhas = con.execute("select data from executor_metadata "
                                 "order by idx desc limit 1").fetchall()
        finally:
            con.close()
    except (sqlite3.Error, OSError):
        return None

    if not linhas:
        return None                  # conversa aberta e ainda sem turno nenhum
    dado = linhas[0][0]
    if isinstance(dado, str):
        dado = dado.encode("utf-8", "replace")
    return bytes(dado) if isinstance(dado, (bytes, bytearray)) else None


def modelo_antigravity(cwd=None, raiz=None):
    """O modelo do Antigravity CLI que roda em `cwd`. None quando nao da para saber.

    Casa pelo diretorio quando da: o blob de `executor_metadata` cita o caminho
    do projeto — medido, em 24 dos 28 bancos com metadados desta maquina. A
    spec original supunha que nao citava, e por isso a sonda devolvia sempre a
    conversa mais recente; dois Antigravity abertos mostravam o mesmo modelo.

    Sem `cwd`, ou quando nenhum banco cita o dele, a reserva continua sendo a
    conversa mais recente — que e a resposta certa justamente quando nao ha
    como separar as duas.

    MAX_IDADE_S vale so na RESERVA, nunca no casamento: banco que cita o
    diretorio do pane e a conversa daquele projeto por mais velho que seja, e o
    que o descartaria por idade nao e uma duvida, e um turno que ninguem mandou.
    O comentario da constante explica por que o contrario apagaria dado certo.
    """
    base = str(raiz or RAIZ_GEMINI)
    padrao = os.path.join(base, "antigravity-cli", "conversations", "*.db")
    limite = _agora() - MAX_IDADE_S

    # O `[:3]` cru de antes sumiu: tres era numero magico e alcancavel — 5 dos
    # 33 bancos daqui tem `executor_metadata` vazia, e uma conversa recem-aberta
    # empurrava a resposta para o segundo banco, de outro projeto, sem sinalizar
    # nada. Quem corta agora e MAX_BANCOS, com o motivo escrito na constante.
    reserva = None
    for f, mtime in _por_recencia(padrao)[:MAX_BANCOS]:
        blob = _metadados(f)
        modelo = _modelo_do_blob(blob) if blob else None
        if not modelo:
            continue            # banco sem turno gravado: pular, nao responder
        if cwd and _blob_cita_pasta(blob, cwd):
            return bonito(modelo)       # casou: a idade nao entra na conta
        if reserva is None and mtime >= limite:
            reserva = modelo    # o mais recente COM modelo, e ainda no teto
        if not cwd:
            # Sem `cwd` nao ha o que casar, e a lista vem ordenada: o primeiro
            # banco com modelo ja decidiu a reserva (ou provou que nem ele esta
            # no teto, e nenhum mais velho estaria). Continuar seria abrir 32
            # bancos para nada.
            break
    return bonito(reserva) if reserva else None
