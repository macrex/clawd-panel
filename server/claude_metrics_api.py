#!/usr/bin/env python3
"""
API local de métricas do Claude Code.

Recebe publicações do statusline.cjs (POST /ingest), mantém o estado das
sessões vivas em memória com TTL, e serve um agregado plano (GET /status)
para consumo por dispositivos na LAN (ex: ESP32-S3).

Sem dependências externas — apenas stdlib.
"""

import collections
import json
import os
import platform
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

import arquivo
import bloqueio
import herdr
import modelos
import motor
import painel
# `planos_mod` e nao `planos`: o nome curto e variavel local em mais de um lugar
# deste arquivo, e uma delas sombrearia o modulo.
import planos as planos_mod
import tela
import terminal
import transcript
import weather
import works

HOST = "0.0.0.0"          # precisa ser 0.0.0.0 para o ESP32 alcançar pela LAN
PORT = 8787


def tag_da_maquina():
    """Como ESTA máquina se chama no painel. Três caracteres, maiúsculos.

    Quem se identifica é o SERVIÇO, e não o cartão da placa. Com duas máquinas
    publicando, a placa precisa dizer de onde veio cada agente — e a alternativa
    (gravar os nomes no `config.json` do SD) obrigava a mexer no cartão, que
    mora dentro da placa, a cada máquina nova. Assim, instalar uma terceira é
    configurá-la nela mesma.

    `CLAWD_TAG` manda; sem ela, as três primeiras letras do hostname, que já
    distinguem `DESKTOP-...` de `NOTE-...` sem ninguém configurar nada. Três é
    teto porque a coluna de chips do painel tem largura fixa.
    """
    bruto = os.environ.get("CLAWD_TAG") or platform.node() or ""
    # Só letras e dígitos: um hostname "PC-CASA" viraria "PC-", e o hífen num
    # chip de três caracteres come um terço do espaço sem informar nada.
    limpo = "".join(c for c in bruto if c.isalnum())
    return limpo[:3].upper()


TAG = tag_da_maquina()

# DUAS FONTES, COM HIERARQUIA CLARA
#
# 1) statusline.cjs (POST /ingest) — heartbeat com os NÚMEROS: contexto, limites,
#    repo, branch, modelo. Só existe enquanto o Claude Code redesenha a barra.
# 2) claude_hook.cjs (POST /event)  — hooks de ciclo de vida com o ESTADO, dito
#    de forma explícita no instante em que muda.
#
# O heartbeat sozinho não distingue "bloqueado esperando você" de "fechado": os
# dois simplesmente calam. Era essa a limitação; os hooks a resolvem. Quando um
# agente para de publicar, agora dá para responder POR QUE parou:
#
#   último evento = Notification -> BLOCKED  (precisa de você)
#   último evento = Stop         -> IDLE     (turno acabou, sem urgência)
#   último evento = SessionEnd   -> some da lista imediatamente
#   nenhum evento                -> UNKNOWN  (sessão sem hooks instalados)
#
# Os hooks são AUTORITATIVOS. O `pid` publicado é meramente informativo: medido
# na prática, ele às vezes aponta para um processo filho transitório já morto
# enquanto a sessão está viva e bloqueada. Remover uma sessão por PID morto
# apagaria justamente o agente que espera por você — o pior erro possível aqui.
# Por isso `proc_alive` é exposto mas nunca decide sozinho (ver prune()).

TTL_SECONDS = 900         # sessão SEM hooks: 15 min sem publicar = removida
HOOKED_TTL = 12 * 3600    # sessão COM hooks: confia-se no SessionEnd para remover
# PID morto + silêncio + estado não-bloqueado = removida. 45s e não 120s: a
# regra só alcança sessão que está calada há mais tempo que o heartbeat de 10s,
# e uma sessão viva e não-bloqueada publica. Fechar a janela no X mata o
# processo sem disparar SessionEnd, e é esse caso que este prazo governa —
# esperar dois minutos para a sessão sumir da tela parecia defeito.
DEAD_PID_GRACE = 45
FRESH_SECONDS = 30        # acima disso: viva, porém sem publicar agora
MAX_BODY = 64 * 1024      # guarda contra payload absurdo

# HORIZONTE DE SANIDADE DOS CARIMBOS DE RESET
#
# A janela de 5h não pode resetar depois de 5h a partir de agora; a de 7 dias não
# pode passar de 7 dias. Um carimbo além disso é lixo, e lixo aqui é caro: a
# regra de escolha dos limites é "maior session_resets_at", então **um** carimbo
# absurdo ganha de todas as sessões reais e vira o rótulo global.
#
# Visto na prática: uma sessão de teste publicou `9999999999` (o sentinela
# clássico de "futuro distante") e o painel passou a mostrar
# `Session 32% 2281821h43m` — 260 anos. As sessões reais estavam corretas o
# tempo todo; só nunca eram consultadas.
#
# A folga cobre relógio torto entre a máquina e o servidor da Anthropic, não
# imprecisão da janela: a janela em si é fixa.
SESSION_HORIZON = 5 * 3600 + 900        # 5h + 15 min
WEEK_HORIZON = 7 * 86400 + 3600         # 7d + 1h

# QUANDO UM `working` DEIXA DE SER ACREDITÁVEL
#
# Os hooks são autoritativos, mas têm dois furos: não existe hook de
# "desbloqueou" nem de "interrompido". Apertar Esc aborta o turno sem disparar
# `Stop`, e a sessão fica `working` para sempre — visto ao vivo, 644 s
# trabalhando com o transcript parado há 675 s e o heartbeat chegando a cada 6 s.
#
# A marca de interrupção no transcript resolve o caso comum, com precisão (ver
# transcript.py). Este prazo é a rede embaixo: pega queda do Claude Code e hook
# perdido no timeout de 400 ms, que não deixam marca nenhuma.
#
# 10 minutos e não 1: uma chamada de ferramenta longa também congela `api_ms`, e
# durante ela a sessão está legitimamente trabalhando. As compilações deste
# projeto levam 40-60 s, então há uma ordem de grandeza de folga. O falso
# positivo que sobra — uma ferramenta de mais de 10 min — se corrige sozinho no
# instante em que `api_ms` volta a andar.
STALL_SECONDS = 600

# CARÊNCIA ANTES DE ACREDITAR NA MARCA DE INTERRUPÇÃO
#
# A marca é gravada no INSTANTE em que você interrompe, mas a linha `assistant`
# que a desmente só aparece quando o Claude volta a responder. No intervalo, uma
# sessão que está trabalhando é indistinguível de uma abortada — e o caso mais
# comum de todos cai bem aí: interromper e já mandar outra coisa. O hook
# `UserPromptSubmit` dispara primeiro, a marca é escrita depois, e a API via uma
# sessão recém-marcada como `working` com uma marca sem resposta.
#
# Aconteceu ao vivo, nesta máquina, com a primeira versão desta correção.
#
# A carência resolve porque `api_ms` separa os dois casos: quem está trabalhando
# chama a API, quem foi abortado não chama. 45 s é folgado para uma sessão viva
# produzir alguma chamada, e ainda detecta a interrupção mais de dez vezes mais
# rápido do que o prazo de STALL_SECONDS.
GRACE_SECONDS = 45

# COMANDOS QUE O PAINEL PODE DISPARAR
#
# Lista BRANCA de comandos exatos, e nao texto livre. O motivo nao e estilo: a
# API escuta em 0.0.0.0 para o ESP alcancar pela LAN, sem autenticacao nenhuma.
# Um endpoint de texto livre seria um teclado aberto na rede, apontado para os
# seus agentes — e `Enter` num prompt de permissao aprova o que estiver sendo
# pedido, inclusive um comando destrutivo.
#
# Com a lista branca, o pior que alguem na rede consegue e compactar a sua
# sessao. Chato, reversivel, e nao destroi trabalho.
#
# `/clear` NAO esta aqui de proposito: ele apaga o contexto sem volta, e entra
# so quando o modelo de confirmacao na tela estiver decidido.
COMANDOS = {
    "compact": "/compact",
}

# Teto da limpeza de contexto. Enquanto ela esta acesa, a turma do rodape varre.
#
# E um TETO, e nao a duracao esperada: o fim de verdade e o contexto encolher.
# Isto existe para o caso em que a prova nunca chega — a sessao parou de
# publicar, o comando caiu num pane errado, o Claude Code pediu confirmacao. Sem
# ele, um unico toque deixaria a turma varrendo para sempre.
LIMPEZA_MAX_S = 240

# Quanto o trabalho espera pelo heartbeat que o fecha.
#
# O `Stop` NAO fecha o trabalho, so o marca pendente. No fechamento o ultimo
# heartbeat pode ser anterior a ultima chamada de API do turno, e fechar ali
# perderia a cauda — justamente a parte mais cara. O primeiro `/ingest` que
# chegar depois traz os contadores completos.
#
# Se nenhum chegar em 30 s, o trabalho fecha com o que ha, marcado `partial`:
# acontece quando a janela e fechada logo apos o turno. Um numero honesto e
# marcado vale mais do que um numero perdido.
FECHAMENTO_MAX_S = 30

# Estados que ESTA API deduziu, e que por isso ela tem o direito de desfazer.
# Um `idle` vindo do hook `Stop` é fato e não se mexe.
DEDUZIDOS = ("interrompido", "parado")

# Estados possíveis, do mais urgente para o menos.
BLOCKED, WORKING, IDLE, UNKNOWN = "blocked", "working", "idle", "unknown"

# session_id -> {
#   "data": dict,         # último payload da statusline (números)
#   "ts": float,          # quando a statusline publicou pela última vez
#   "state": str,         # estado corrente
#   "state_ts": float,    # quando esse estado foi declarado
#   "event": str,         # QUEM produziu o estado: o nome do hook, ou
#                         # "api-ativa"/"interrompido"/"parado" quando foi a
#                         # própria API que corrigiu (ver corrigir_estado)
#   "api_ms": float,      # cumulativo de tempo em chamada de API
#   "api_ms_ts": float,   # quando `api_ms` mudou pela última vez
#   "transcript_path": str,
#   "hooked": bool,       # esta sessão já falou por hook alguma vez?
#   "pid": int,
# }
_sessions = {}
_lock = threading.Lock()

# sid -> (inicio, avisado): memoria de divergencia entre nos e o herdr, para
# que build_agents avise UMA vez por episodio em vez de a cada /status (ver
# divergencia()). Fica no modulo, e nao em _sessions, porque nao e estado de
# sessao — e estado da COMPARACAO entre dois observadores, e sobrevive mesmo
# que _sessions seja reescrito.
_divergencias = {}

# Trava PROPRIA, separada de `_lock`. `fundir` (e portanto `divergencia`) roda
# em build_agents, que e chamado FORA do `with _lock:` de build_status — de
# proposito, para nao segurar o lock principal durante o desenho da resposta
# inteira. O servidor e ThreadingHTTPServer com uma thread por conexao, e o
# painel bate em /status a cada 2s: sem trava propria aqui, duas requisicoes
# concorrentes podiam intercalar o get-decide-set de `divergencia()` e sair
# dois registros do mesmo episodio, ou ressuscitar uma concordancia com uma
# escrita atrasada.
#
# AS DUAS TRAVAS SE ANINHAM, SIM — em `prune()` e em `_handle_event()`, os
# dois lugares que apagam uma sessao e por isso precisam limpar `_divergencias`
# junto. A invariante que evita deadlock nao e "ninguem aninha": e a ORDEM ser
# sempre a mesma. **Sempre `_lock` por fora, `_div_lock` por dentro — nunca o
# inverso.** Adquirir `_lock` estando dentro de um bloco `with _div_lock:`
# inverteria a ordem e é o jeito clássico de travar o servidor inteiro.
_div_lock = threading.Lock()

# Ultimas divergencias registradas, para /health. Em memoria e nao em disco:
# isto e diagnostico de quem esta olhando agora, nao estado que precisa
# sobreviver a reinicio. `print()` sozinho nao chega a lugar nenhum em
# producao — a API sobe como pythonw.exe destacado (ver tray.pyw), sem
# console e sem redirecionamento, entao sys.stdout e None e o print retorna
# sem escrever nada. maxlen=20: quem quer ver mais que isso quer um log de
# verdade, nao um campo de /health.
DIVERGENCIA_LOG_MAX = 20
_divergencia_log = collections.deque(maxlen=DIVERGENCIA_LOG_MAX)


def _now():
    return time.time()


# ---- Persistência ----
# O estado vive em memória, mas NÃO pode morrer com o processo. Um agente
# bloqueado não consegue se reanunciar: ele parou de publicar justamente porque
# está travado esperando você. Se a API reiniciar, ele desaparece do painel e só
# volta quando você mexe nele — ou seja, quando o aviso já não serve para nada.
#
# Os carimbos são epoch absoluto, então sobrevivem ao reinício sem ajuste; o
# prune() na carga descarta o que envelheceu demais enquanto a API esteve fora.
STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "state.json")
SAVE_MIN_INTERVAL = 1.0     # o heartbeat é frequente e recuperável: pode esperar
_last_save = 0.0


def save_state(force=False):
    """Grava o estado em disco. Chamado sob _lock.

    `force=True` nos eventos de hook: são raros e é neles que mora a informação
    que não dá para recuperar sozinha. O heartbeat pode ser engolido por um
    crash sem prejuízo — a próxima statusline o traz de volta em 10s.
    """
    global _last_save
    now = _now()
    if not force and (now - _last_save) < SAVE_MIN_INTERVAL:
        return
    _last_save = now
    tmp = STATE_FILE + ".tmp"
    try:
        # Escreve-e-renomeia: um crash no meio deixa o arquivo antigo intacto,
        # nunca um JSON pela metade que faria a próxima carga falhar.
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump({"sessions": _sessions}, fh)
        os.replace(tmp, STATE_FILE)
    except (OSError, TypeError, ValueError):
        pass    # persistência é conveniência: nunca pode derrubar a API


def load_state():
    """Recarrega o estado salvo. Só no boot, antes de servir."""
    try:
        with open(STATE_FILE, encoding="utf-8") as fh:
            saved = json.load(fh)["sessions"]
    except (OSError, ValueError, KeyError, TypeError):
        return 0
    with _lock:
        for sid, rec in saved.items():
            if isinstance(rec, dict) and "ts" in rec:
                # "waiting" era o nome antigo de "idle". Sem esta migração, uma
                # sessão gravada antes da renomeação voltaria com um estado que
                # nada mais reconhece, e ficaria no fim da ordenação para sempre.
                if rec.get("state") == "waiting":
                    rec["state"] = IDLE
                _sessions[sid] = rec
        prune()     # descarta o que apodreceu enquanto a API esteve fora
        return len(_sessions)


def pid_alive(pid):
    """O processo ainda existe? None quando não dá para saber.

    Só ctypes/stdlib — a API não tem dependências externas de propósito.
    """
    if not pid:
        return None
    try:
        if sys.platform == "win32":
            import ctypes
            PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
            STILL_ACTIVE = 259
            k = ctypes.windll.kernel32
            handle = k.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
            if not handle:
                return False
            try:
                code = ctypes.c_ulong()
                if k.GetExitCodeProcess(handle, ctypes.byref(code)) == 0:
                    return None
                return code.value == STILL_ACTIVE
            finally:
                k.CloseHandle(handle)
        # POSIX: sinal 0 não entrega nada, só valida a existência do processo.
        os.kill(int(pid), 0)
        return True
    except PermissionError:
        return True        # existe, apenas não é nosso
    except (OSError, ValueError, AttributeError):
        return False


def fmt_hm(seconds):
    if seconds < 0:
        seconds = 0
    h = int(seconds // 3600)
    m = int((seconds % 3600) // 60)
    return f"{h}h{m:02d}m"


def fmt_dh(seconds):
    if seconds < 0:
        seconds = 0
    d = int(seconds // 86400)
    h = int((seconds % 86400) // 3600)
    return f"{d}d{h:02d}h"


# Dias por extenso, em ASCII pelo mesmo motivo dos abreviados de weather.py: a
# fonte embutida do Arduino_GFX so tem ASCII, entao "Sábado" sairia com lixo no
# lugar do acento.
DIAS_EXTENSO = ("Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado",
                "Domingo")


def fmt_clock(ts):
    """Carimbo absoluto -> "7:20pm", no fuso desta maquina.

    O prazo relativo ("4h24m") responde quanto falta; este responde QUANDO, que
    e a pergunta de quem esta decidindo se comeca mais uma coisa hoje. Os dois
    juntos porque nenhum dos dois substitui o outro.

    Fuso: a conversao acontece aqui, e nao no painel, porque a placa nao tem
    relogio nem fuso — e esta maquina e a mesma que roda o Claude Code que
    produziu o carimbo.

    O -1s NAO e ajuste de fuso nem gambiarra de arredondamento: o que se mostra
    e o ULTIMO MINUTO EM QUE A JANELA AINDA VALE, que e a mesma convencao do
    `/cost`. Os carimbos da API chegam redondos (15:10:00, 23:00:00), entao a
    janela que vira as 15:10 em ponto vale ate 15:09:59 — o terminal escreve
    "Resets 3:09pm" e o painel escrevia "3:10pm" ao lado dele, o que le como
    conta errada. Carimbo quebrado (15:09:30) formata igual com ou sem o -1s.
    """
    t = time.localtime(ts - 1)
    h = t.tm_hour % 12 or 12
    return f"{h}:{t.tm_min:02d}{'pm' if t.tm_hour >= 12 else 'am'}"


def fmt_date(ts):
    """Carimbo absoluto -> "01/08/2026 (Sabado)".

    O dia da semana vai junto de proposito: "01/08" sozinho obriga a consultar
    um calendario para saber se o limite volta antes ou depois do fim de semana.
    """
    t = time.localtime(ts)
    return (f"{t.tm_mday:02d}/{t.tm_mon:02d}/{t.tm_year} "
            f"({DIAS_EXTENSO[t.tm_wday]})")


def valid_reset(ts, now, horizon):
    """Carimbo de reset plausível? Devolve 0 quando não.

    Duas rejeições, e as duas importam:
      - passado  -> janela já expirou, o snapshot é lixo velho;
      - futuro além do horizonte -> carimbo impossível para o tamanho da janela.
    """
    if not isinstance(ts, (int, float)) or isinstance(ts, bool):
        return 0
    return int(ts) if now < ts <= now + horizon else 0


def pick_window(live, campo_ts, campo_pct, horizon, now):
    """Melhor snapshot para UMA janela. Devolve (pct, resets_at) ou None.

    Escolhe por (maior carimbo, maior percentual): o maior carimbo é a janela
    vigente, e dentro dela o maior percentual é a leitura mais recente, porque
    dentro de uma janela fixa o uso só cresce.

    Cada janela é escolhida SEPARADAMENTE, e essa separação é o conserto de um
    defeito real: um único registro ditava as duas, então quando a janela de 5h
    vencia a semana ia para zero junto — mesmo estando válida por mais dois
    dias. Nada obriga o melhor snapshot de 5h e o melhor semanal a virem da
    mesma sessão.
    """
    validos = [r for r in live if valid_reset(r["data"].get(campo_ts), now, horizon)]
    if not validos:
        return None
    ref = max(validos, key=lambda r: (r["data"][campo_ts],
                                      r["data"].get(campo_pct) or 0))
    return (ref["data"].get(campo_pct) or 0, int(ref["data"][campo_ts]))


def expired_window_seen(live, campo_ts, now):
    """Alguma sessão carrega um carimbo desta janela que JÁ venceu?

    Distingue "a janela virou" de "nunca soubemos de janela nenhuma". Só o
    primeiro caso autoriza concluir que o uso recomeçou.
    """
    for r in live:
        ts = r["data"].get(campo_ts)
        if isinstance(ts, (int, float)) and not isinstance(ts, bool) and 0 < ts <= now:
            return True
    return False


def resolver_sessao(sessions, sid):
    """Resolve o id que o painel mandou para a chave real. None se nao der.

    O painel so conhece os 8 primeiros caracteres — e o que `labels` publica,
    porque o id inteiro nao cabe na tela e nao serve para nada la. Entao o
    prefixo tem que ser aceito aqui.

    Prefixo AMBIGUO devolve None de proposito: mandar o comando para a sessao
    errada e pior do que nao mandar comando nenhum.
    """
    if not sid:
        return None
    if sid in sessions:
        return sid
    achados = [k for k in sessions if k.startswith(sid)]
    return achados[0] if len(achados) == 1 else None


def limpando(rec, now):
    """Esta sessao esta compactando contexto AGORA?

    Duas saidas, e as duas importam:

      - o contexto ENCOLHEU em relacao ao que era quando o comando saiu. Essa e
        a prova direta de que a compactacao terminou, e nao um palpite de tempo;
      - o prazo estourou. Sem prova nenhuma, a API para de afirmar.

    Nao entra `state` nesta conta: durante uma compactacao o estado publicado
    nao e garantido, e o painel nao pode ficar piscando por causa disso.
    """
    ts = rec.get("limpeza_ts")
    if not isinstance(ts, (int, float)) or isinstance(ts, bool):
        return False
    if now - ts > LIMPEZA_MAX_S:
        return False
    ref = rec.get("limpeza_ctx")
    ctx = (rec.get("data") or {}).get("context_pct")
    if (isinstance(ref, (int, float)) and not isinstance(ref, bool)
            and isinstance(ctx, (int, float)) and not isinstance(ctx, bool)
            and ctx < ref):
        return False
    return True


def pct_color(pct):
    """Mesma regra da statusline: <50 verde, 50-79 amarelo, >=80 vermelho."""
    if pct >= 80:
        return "red"
    if pct >= 50:
        return "yellow"
    return "green"


def short_model(name):
    """"Opus 5 (1M context)" -> "Opus 5 (1M)".

    A palavra "context" dentro do parêntese não informa nada: o que está entre
    parênteses num nome de modelo já é o tamanho da janela. Ela custa 8
    caracteres, e no painel a linha do modelo tem 18 — sem encurtar, o nome
    sozinho já era truncado e não sobrava espaço nenhum para o esforço.
    """
    if not name:
        return name
    return name.replace(" context)", ")")


def agent_label(agent):
    """Nome do agente do herdr, pronto para exibir. None quando desconhecido.

    Usado só na `line` de um órfão (ver build_agents): sem isto, `agent_line`
    cai no default `"Claude"` de `short_model` e um agente do codex sai
    afirmando, na própria linha, que é o Claude — falso, com o campo `agent`
    correto bem ao lado.
    """
    if not agent:
        return None
    return agent.capitalize()


# Esforço de raciocínio, como o Claude Code o nomeia. O rótulo é montado AQUI e
# não no firmware: quem conhece o vocabulário é quem fala com o Claude Code, e
# `xhigh` -> `XHigh` é uma regra que nenhuma capitalização automática acerta.
EFFORT_LABELS = {
    "low": "Low", "medium": "Medium", "high": "High",
    "xhigh": "XHigh", "max": "Max",
}


def effort_label(raw):
    """Nível pronto para imprimir. None quando a sessão não reporta."""
    if not raw:
        return None
    return EFFORT_LABELS.get(str(raw).lower(), str(raw).capitalize())


def agent_line(repo, branch, model, context_pct, effort=None):
    """Linha pronta para imprimir, de UM agente."""
    head = f"❖ {repo}" if repo else "❖ —"
    if branch:
        head += f"  ⎇ {branch}"
    ctx = f"Context {context_pct}%" if context_pct is not None else "Context —"
    nome = short_model(model) or "Claude"
    if effort:
        nome += f" {effort}"
    return f"{head} | {nome} | {ctx}"


def limits_labels(session_pct, session_hm, week_pct, week_dh,
                  session_known, week_known):
    """Rótulos dos limites de conta. NÃO são por agente — são globais.

    Cada janela é rotulada por si: uma vencida não apaga a outra. E o
    percentual pode ser conhecido sem o prazo — é o caso logo depois da virada
    da janela de 5h, quando se sabe que o uso zerou mas não quando a próxima
    janela termina.
    """
    def rotulo(nome, pct, prazo, conhecido):
        if not conhecido:
            return f"{nome} —"
        if prazo and prazo != "-":
            return f"{nome} {pct}% {prazo}"
        return f"{nome} {pct}%"

    return (rotulo("Session", session_pct, session_hm, session_known),
            rotulo("Week", week_pct, week_dh, week_known))


# ---- O plano de cada conta, e o modelo de quem so o herdr enxerga ----
#
# As duas informacoes vem de DISCO — do que cada CLI grava por conta propria
# (ver planos.py e modelos.py) — e as duas sao consumidas de dentro do /status,
# que a placa pede a cada dois segundos. Sao os caches abaixo que fazem isso ser
# possivel: sem eles, cada poll pagaria a varredura inteira.

# O plano de conta muda uma vez por ano; reler o disco a cada /status seria
# desperdicio puro. Uma hora e folgado e ainda pega a troca de plano no mesmo
# dia em que ela acontece.
PLANOS_TTL_S = 3600.0

# O modelo muda a cada turno, e a leitura custa entre 2 e 75 ms conforme o
# numero de panes — caro demais para o caminho de uma requisicao que responde
# em 2 a 14 ms. Meio minuto e curto o bastante para a troca de modelo aparecer
# antes de virar mentira na tela, e longo o bastante para o custo sumir na
# media: uma leitura a cada quinze polls.
MODELOS_TTL_S = 30.0

# Quando parar para varrer as entradas vencidas de `_modelos_cache`.
#
# O cache tem uma entrada por (agente, cwd) VISTO, e o servidor sobe no logon e
# roda por meses: sem poda nenhuma, os diretorios de ontem ficam ali para
# sempre. Nao e vazamento agudo — sao alguns panes por dia, de algumas dezenas
# de bytes cada — e por isso o remedio e o mais barato que resolve: entrada
# vencida nunca mais e servida, entao joga-las fora quando o dicionario passa do
# teto nao perde nada e dispensa LRU, thread de limpeza e contabilidade de
# acesso.
#
# E um GATILHO, e nao um teto rigido: 64 panes vivos simultaneos manteriam 64
# entradas frescas e nenhuma seria podada — que e o certo, porque todas estao em
# uso. O que o numero garante e que o dicionario nao acumula os MORTOS.
MODELOS_CACHE_MAX = 64

_planos_cache = {"ts": 0.0, "valor": {}}
_modelos_cache = {}      # (agente, cwd) -> {"ts": float, "valor": str|None}

# Quantas vezes a sonda de modelo LEVANTOU desde o boot, exposto em /health.
#
# Existe porque `None` e uma resposta ambigua: a sonda devolve None quando nao
# acha nada (o caso normal — um pane de Codex que ainda nao gravou rollout) e o
# `except` de `modelo_do_orfao` tambem devolve None quando ela explode. Os dois
# desenham o mesmo "—" na tela, e de fora nao havia como separar "nao casou" de
# "esta quebrada desde o deploy" — pagando ~68 ms a cada 30 s para descobrir.
#
# Contador e nao lista, e sem `print`: `print` nao chega a lugar nenhum sob
# pythonw.exe destacado (sys.stdout is None), que e a mesma razao de
# `_divergencia_log` existir. A pergunta que se faz do /health e "esta
# explodindo?", e para isso um numero que so cresce basta.
_sondas_falhas = 0

# Trava so dos dois caches acima e do contador. Quem a ADQUIRE sao apenas
# `build_planos` e `modelo_do_orfao` — nao confunda com `build_agents` e o
# `return` de `build_status`, que sao quem CHAMA essas duas.
#
# NAO aninha com `_lock` nem com `_div_lock`: os dois chamadores rodam FORA do
# `with _lock:` de build_status, de proposito (ver o comentario de `_div_lock`
# sobre por que a ordem das travas importa neste arquivo). E o disco fica fora
# dela: so as operacoes de dicionario ficam dentro.
#
# Existe pelo mesmo motivo de `_div_lock`: o servidor e ThreadingHTTPServer, uma
# thread por conexao, e a poda de `_modelos_cache` itera o dicionario — iterar
# enquanto outra thread escreve levanta RuntimeError e derrubaria o /status.
_cache_lock = threading.Lock()


def _ler_json(caminho):
    """Um JSON do disco. `None` quando nao deu — ausente, ilegivel ou quebrado.

    `None` e nao `{}` de proposito: quem chama precisa distinguir "o arquivo nao
    existe" de "existe e esta vazio". `build_planos` depende disso — o
    `isinstance(auth, dict)` do caminho do Codex so pergunta pelo `id_token`
    quando houve arquivo de verdade, em vez de mergulhar num vazio inventado
    aqui. Quem quiser o `{}` converte na sua ponta, como `_planos_json` faz.
    """
    try:
        with open(os.path.expanduser(caminho), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _planos_json():
    """O `planos.json` da instalacao. Ausente e o caso NORMAL — quem so usa
    provedores que publicam o plano em disco nunca precisa criar o arquivo.

    Sempre um dicionario: `plano_declarado` itera as chaves, e um `None` aqui
    obrigaria toda chamada a se defender do mesmo jeito.
    """
    d = _ler_json(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "planos.json"))
    return d if isinstance(d, dict) else {}


def build_planos():
    """{"claude": "Max 5x", "codex": "Free", "agy": "AI Pro"}.

    As chaves sao os nomes de agente DO HERDR — os mesmos que saem em
    `labels[].agent`. E por isso que o Antigravity aparece como `agy`: as duas
    pontas tem que falar o mesmo vocabulario para quem desenha poder casar o
    plano com o grupo. Ver o comentario em `modelo_do_orfao`.

    Provedor sem plano conhecido simplesmente NAO entra no dicionario: a placa
    desenha "—" para a chave ausente, e uma string vazia no fio seria a mesma
    coisa custando bytes.

    O dicionario devolvido e O DO CACHE, e nao uma copia: muta-lo corrompe a
    memoria por uma hora inteira. E o mesmo acordo de `vitalicio_do_livro` e de
    `works.ler_cache` neste arquivo — ninguem mexe no que eles devolvem.
    """
    agora = _now()
    with _cache_lock:
        if agora - _planos_cache["ts"] < PLANOS_TTL_S:
            return _planos_cache["valor"]

    # O disco fica FORA da trava: duas threads que percam a corrida leem o mesmo
    # arquivo duas vezes uma vez por hora, o que e barato; segurar a trava
    # durante a leitura pararia a outra requisicao por ela.
    declarados = _planos_json()
    fora = {}

    p = planos_mod.plano_claude(_ler_json("~/.claude.json"))
    if p:
        fora["claude"] = p

    auth = _ler_json("~/.codex/auth.json")
    if isinstance(auth, dict):
        p = planos_mod.plano_codex((auth.get("tokens") or {}).get("id_token"))
        if p:
            fora["codex"] = p

    # Declarado a mao NUNCA sobrescreve o que foi descoberto: se um dia o Claude
    # passar a publicar o plano num campo novo, a linha velha do planos.json nao
    # pode mentir por cima dele.
    for agente in declarados:
        if agente not in fora:
            p = planos_mod.plano_declarado(declarados, agente)
            if p:
                fora[agente] = p

    with _cache_lock:
        _planos_cache["ts"] = agora
        _planos_cache["valor"] = fora
    return fora


def _podar_modelos(agora):
    """Joga fora as entradas vencidas de `_modelos_cache`. Chamado sob a trava."""
    if len(_modelos_cache) < MODELOS_CACHE_MAX:
        return
    for chave in [k for k, v in _modelos_cache.items()
                  if agora - v["ts"] >= MODELOS_TTL_S]:
        _modelos_cache.pop(chave, None)


def modelo_do_orfao(agente, cwd):
    """O modelo de um agente que so o herdr enxerga. None quando nao se sabe."""
    global _sondas_falhas
    chave = (agente, cwd)
    agora = _now()
    with _cache_lock:
        c = _modelos_cache.get(chave)
        if c and agora - c["ts"] < MODELOS_TTL_S:
            return c["valor"]

    falhou = False
    try:
        if agente == "codex":
            v = modelos.modelo_codex(cwd)
        # `agy` E O NOME. Nao e abreviacao nossa nem apelido: e como o herdr
        # nomeia o Antigravity, e por isso e o que chega em `labels[].agent` e o
        # que se escreve no `planos.json`. O nome do PRODUTO nao aparece em
        # lugar nenhum deste caminho de proposito — traduzir `agy` para
        # "antigravity" exigiria uma tabela dos 21 kinds do herdr para alguem
        # manter sincronizada, e a primeira CLI nova passaria sem traducao sem
        # ninguem notar ate a tela mentir. NAO "conserte" para "antigravity":
        # esse nome nunca chega aqui, e o alias so sugeriria um caminho que nao
        # existe. Ha teste travando isto.
        #
        # `gemini` NAO entra: e outro kind do herdr, o Gemini CLI, e a sonda do
        # Antigravity tem reserva ("a conversa mais recente"). Roteado para ca,
        # um pane de Gemini CLI mostraria o modelo de OUTRO programa com cara de
        # certo — pior do que "—".
        elif agente == "agy":
            v = modelos.modelo_antigravity(cwd)
        else:
            v = None
    except Exception:
        # Os modulos ja prometem nao levantar. Este except e o cinto de
        # seguranca: um defeito neles nao pode derrubar o /status inteiro.
        #
        # Mas engolir em silencio faria o defeito virar um "—" indistinguivel do
        # "nao achei nada", entao ele deixa RASTRO — ver `_sondas_falhas`. O
        # `None` continua sendo o que a placa recebe: nao ha resposta melhor, e
        # quem quer saber se a sonda quebrou pergunta ao /health.
        v = None
        falhou = True

    with _cache_lock:
        if falhou:
            _sondas_falhas += 1
        _podar_modelos(agora)
        _modelos_cache[chave] = {"ts": agora, "valor": v}
    return v


def build_agents(live_items, retrato=None):
    """Uma entrada por agente vivo, do maior contexto para o menor.

    Cada agente do Claude Code tem o SEU proprio contexto, repo, branch e
    modelo. Por isso isto e uma LISTA e nao um agregado: com varios diretorios
    abertos ao mesmo tempo, escolher "o de maior contexto" e mostrar so ele
    escondia todos os outros.

    Os limites de 5h e 7 dias ficam de fora de proposito: sao globais da conta,
    iguais para todos os agentes, e repeti-los em cada entrada sugeriria que
    pertencem ao agente.

    `retrato`: o snapshot do herdr desta resposta. `build_status` tira UM e
    passa aqui, para que ele e o `herdr_online` do topo sejam sempre o mesmo
    instante — a thread de fundo reescreve o retrato a cada 2s, e duas
    chamadas separadas a `herdr.snapshot()` na mesma resposta podiam pegar
    dois instantes diferentes (`labels` fundido com um retrato online e
    `herdr_online: false` no topo, ou o inverso). Default None tira o próprio
    retrato, para quem ainda chama `build_agents(live_items)` sozinho.
    """
    if retrato is None:
        retrato = herdr.snapshot()
    estados, concluidos, orfaos = fundir(
        [{"sid": sid,
          "pane_id": rec.get("herdr_pane_id"),
          "state": resolve_state(rec, _now())} for sid, rec in live_items],
        retrato["agents"] if retrato["online"] else [],
        now=_now(), memoria=_divergencias)

    agents = []
    now = _now()
    for sid, rec in live_items:
        d = rec["data"]
        age = int(now - rec["ts"])
        raw = d.get("context_pct")
        # Uma sessao pode publicar sem context_pct (visto na pratica). Vira
        # None e vai para o fim da lista, em vez de virar 0 e mentir.
        pct = int(round(raw)) if isinstance(raw, (int, float)) else None
        repo, branch = d.get("repo"), d.get("branch")
        model = short_model(d.get("model"))
        # Cada agente tem o SEU esforço: dá para ter um em `high` e outro em
        # `low` ao mesmo tempo, e é por isso que ele mora aqui e não só no topo.
        effort = effort_label(d.get("effort"))
        state = estados.get(sid, resolve_state(rec, now))
        agents.append({
            "session_id": sid[:8],       # distingue dois agentes no mesmo repo
            "repo": repo,
            "branch": branch,
            "model": model,
            # Já capitalizado para exibição; o valor cru continua visível em
            # /sessions. None quando a sessão publica sem o campo (statusline
            # anterior a esta versão).
            "effort": effort,
            "context_pct": pct,
            "color": pct_color(pct) if pct is not None else "green",
            "age": age,
            # Estado dito pelos hooks. É este campo que o painel usa para decidir
            # o que pulsa — só `blocked` merece a atenção do usuário.
            "state": state,
            # Terminou e voce ainda nao viu. So o herdr sabe distinguir isto de
            # um `idle` qualquer, porque so ele sabe o que foi olhado na tela.
            "done": sid in concluidos,
            "agent": "claude",
            "state_age": int(now - rec["state_ts"]) if rec.get("state_ts") else None,
            "event": rec.get("event"),
            # O heartbeat calou. Diz só isso — o motivo mora em `state`.
            # Chamava-se `idle` e o nome mentia: uma sessão pode estar ociosa
            # publicando normalmente, e pode estar trabalhando sem publicar.
            "stale": age > FRESH_SECONDS,
            "proc_alive": rec.get("proc_alive"),
            # Contadores CUMULATIVOS da sessao, desde que ela abriu. Chegavam
            # na statusline e paravam aqui: `/sessions` os mostrava, `/status`
            # nao, entao o painel nunca os viu.
            #
            # Sao da SESSAO e nao do dia. Uma sessao aberta ha oito horas soma
            # oito horas; fechar o terminal zera. Quem exibir precisa dizer isso
            # — e por isso nao ha soma entre sessoes aqui.
            #
            # None quando a sessao nao publica o campo (statusline anterior a
            # esta versao): zero seria uma afirmacao, e nao se sabe.
            "cost_usd": d.get("cost_usd"),
            "lines_added": d.get("lines_added"),
            "lines_removed": d.get("lines_removed"),
            "api_ms": d.get("api_ms"),
            # O pane do herdr, quando esta sessao tem um. E por ele que se le a
            # tela de um agente bloqueado e se responde a pergunta dele — ver
            # bloqueio.atual. None numa sessao que o herdr nao enxerga: ela
            # continua aparecendo no painel, so nao da para responder por ela.
            "pane_id": rec.get("herdr_pane_id"),
            "line": agent_line(repo, branch, model, pct, effort),
        })

    # Agentes que o herdr ve e nos nao: codex, gemini, cursor, ou um Claude Code
    # aberto antes desta versao. Entram com o que ha, e o painel ja desenha "-"
    # nos campos que faltam (src/ui.cpp:362).
    for a in orfaos:
        estado = DO_HERDR.get(a["state"], UNKNOWN)
        # O modelo do orfao vem do disco da CLI dele — ver modelos.py. Era None
        # fixo, e o card desenhava "-" para todo agente que nao fosse o Claude.
        modelo_orfao = modelo_do_orfao(a["agent"], a["cwd"])
        agents.append({
            "session_id": a["pane_id"],
            "repo": a["repo"],
            "branch": None,
            "model": modelo_orfao,
            "effort": None,
            "context_pct": None,
            "color": "green",
            "age": 0,
            "state": estado,
            "done": a["state"] == "done",
            "agent": a["agent"],
            "state_age": None,
            "event": "herdr",
            "stale": False,
            "proc_alive": None,
            # Um orfao e um agente que o herdr ve na tela e nos nao conhecemos:
            # nao ha statusline nossa publicando por ele, entao nao ha custo nem
            # linhas nem tempo de API para relatar. As chaves existem mesmo
            # assim, em None: `labels` tem que ter a MESMA forma para toda
            # entrada, senao o firmware passa a ler um campo que existe para uns
            # e nao para outros. (Ha prova disso em test_integracao.)
            "cost_usd": None,
            "lines_added": None,
            "lines_removed": None,
            "api_ms": None,
            # Um orfao E o pane: o herdr e a unica fonte que temos dele. Aqui
            # `pane_id` sempre existe, ao contrario das nossas sessoes.
            "pane_id": a["pane_id"],
            "line": agent_line(a["repo"], None,
                               modelo_orfao or agent_label(a["agent"]),
                               None, None),
        })

    # Quem precisa de você primeiro; depois, maior contexto. Um agente bloqueado
    # no topo da lista é a informação mais acionável que este painel tem.
    order = {BLOCKED: 0, WORKING: 1, IDLE: 2, UNKNOWN: 3}
    agents.sort(key=lambda a: (order.get(a["state"], 3),
                               a["context_pct"] is None,
                               -(a["context_pct"] or 0)))
    return agents


def resolve_state(rec, now):
    """Estado atual de uma sessão. Só devolve o que já foi decidido.

    Esta função já corrigiu o estado aqui dentro, comparando o `context_pct`
    atual com o do momento do bloqueio. Duas coisas estavam erradas nisso.

    A conta não funcionava. Com janela de 1M tokens, um ponto percentual são
    10.000 tokens: aprovar uma permissão, rodar duas ferramentas e responder não
    move o inteiro, e a sessão ficava `blocked` até o `Stop`. Pior, uma sessão que
    publicou `context_pct: None` no instante do bloqueio guardava `state_ctx =
    None`, a comparação nunca era verdadeira e o desbloqueio virava impossível.

    E o lugar estava errado. Isto roda no caminho de LEITURA, chamado a cada
    `/status` — o ESP consulta a cada 2 s. Correção de estado não pode morar num
    caminho que só deveria olhar. Mudou para o `/ingest`, onde o heartbeat novo
    chega e onde dá para gravar a decisão de verdade, em vez de maquiar na saída.
    """
    return rec.get("state") or UNKNOWN


# Estados do herdr que aceitamos, mapeados para os nossos. `done` e o mesmo
# `idle` de trabalho que terminou sem ser visto — ele nao vira um estado novo no
# `/status` porque a placa mapeia string desconhecida para Unknown
# (lib/metrics/status.cpp:20), e um agente que TERMINOU apareceria como "estado
# desconhecido". Vai como `idle`, com a informacao num campo ao lado.
DO_HERDR = {
    "working": WORKING,
    "blocked": BLOCKED,
    "idle": IDLE,
    "done": IDLE,
}


# Quanto tempo os dois podem discordar antes de virar noticia. Eles nao mudam de
# estado no mesmo instante — o herdr le a tela e nos ouvimos hooks — entao
# discordar por alguns segundos e normal.
DIVERGENCIA_S = 30


def divergencia(sid, nosso, dele, now, memoria):
    """Chegou a hora de registrar que os dois discordam sobre esta sessao?

    Devolve True UMA vez por episodio. Concordar limpa a memoria, e o proximo
    desacordo comeca a contar de novo — senao uma divergencia que dura minutos
    inundaria a saida a cada consulta do /status (o painel bate a cada 2s).

    O get-decide-set inteiro fica atras de `_div_lock`: duas requisicoes
    concorrentes lendo e escrevendo a mesma entrada de `memoria` sem trava
    podiam sair dois avisos do mesmo episodio, ou reviver um episodio ja
    encerrado. Os testes chamam esta funcao com um dict local (`self.mem`) em
    vez da memoria de modulo — a trava e module-level e nao por instancia de
    memoria, mas como os testes rodam numa thread so, isso e transparente.
    """
    with _div_lock:
        if nosso == dele:
            memoria.pop(sid, None)
            return False
        inicio, avisado = memoria.get(sid, (now, False))
        if avisado:
            memoria[sid] = (inicio, True)
            return False
        if (now - inicio) < DIVERGENCIA_S:
            memoria[sid] = (inicio, False)
            return False
        memoria[sid] = (inicio, True)
        return True


def fundir(nossas, agentes, now=None, memoria=None):
    """Casa as nossas sessoes com o retrato do herdr, pelo `pane_id`.

    O herdr ganha quando tem certeza: ele le a tela renderizada e nao depende de
    hook nenhum, e foi justamente a falta de hook que prendeu duas sessoes no
    estado errado em 31/07. Quando ele diz `unknown`, ou quando nao ha par, vale
    a nossa maquina de estados — que continua sendo o unico caminho para sessoes
    abertas fora do herdr.

    `SessionEnd` continua soberano e nao passa por aqui: o herdr ve um pane
    vazio, nao uma sessao encerrada.

    `now`/`memoria` sao opcionais e default None: assim a funcao continua pura
    para quem so quer fundir (os testes da Tarefa 4), e so registra divergencia
    sustentada quando quem chama passa uma memoria de verdade (build_agents, com
    a memoria de modulo `_divergencias`).

    Devolve (estados, concluidos, orfaos).
    """
    por_pane = {}
    for a in agentes:
        por_pane[a["pane_id"]] = a       # o ultimo vence; panes sao unicos

    estados = {}
    concluidos = set()
    pareados = set()

    for n in nossas:
        sid = n["sid"]
        estados[sid] = n["state"]
        pane = n.get("pane_id")
        if not pane:
            continue
        a = por_pane.get(pane)
        if not a:
            continue
        pareados.add(pane)
        traduzido = DO_HERDR.get(a["state"])
        if traduzido is None:
            continue                     # `unknown`, ou um estado que nao conhecemos
        if memoria is not None and traduzido != n["state"]:
            if divergencia(sid, n["state"], traduzido, now or _now(), memoria):
                # print() é best-effort — serve quem roda a API de um
                # terminal. Em produção (pythonw.exe destacado, sys.stdout is
                # None) ele não escreve nada, então o registro que sobrevive é
                # este: _divergencia_log, lido por /health.
                _divergencia_log.append({
                    "sid": sid, "nosso": n["state"], "herdr": traduzido,
                    "pane": pane, "ts": now or _now(),
                })
                print(f"divergencia: {sid} nos={n['state']} herdr={traduzido} "
                      f"pane={pane}", flush=True)
        estados[sid] = traduzido
        if a["state"] == "done":
            concluidos.add(sid)

    orfaos = [a for pane, a in por_pane.items() if pane not in pareados]
    return estados, concluidos, orfaos


def corrigir_estado(rec, now):
    """Confere o estado declarado pelos hooks contra o que a máquina mostra.

    Chamada do `/ingest`, sob o lock, depois de `rec["data"]` já ter o heartbeat
    novo. Devolve o nome de quem corrigiu, ou None quando nada mudou.

    Os hooks continuam autoritativos: esta função só age nas arestas para as
    quais não existe hook nenhum.

      blocked  -> working   `api_ms` andou. Só anda com chamada de API, então é
                            prova de que o Claude voltou a produzir.
      working  -> idle      o turno foi interrompido (marca no transcript) ou
                            `api_ms` está congelado além do prazo.
      deduzido -> working   TODA dedução desta função é reversível pela mesma
                            prova. Sem isto, um engano nosso duraria o turno
                            inteiro, porque nada mais mexe num `idle`.
    """
    estado = rec.get("state")
    quem = rec.get("event")

    # A ÚNICA prova usada aqui: quando houve a última chamada de API. Comparar o
    # instante, e não o valor de `api_ms` contra uma referência guardada, tem
    # duas vantagens — não precisa de campo novo no registro (um `state.json`
    # gravado por qualquer versão anterior já funciona) e não trava quando esse
    # campo falta. A primeira versão desta correção guardava a referência e
    # travou exatamente assim, ao vivo, nesta máquina.
    mexeu = rec.get("api_ms_ts")
    desde = rec.get("state_ts") or 0

    # Estado parado que NÓS deduzimos, ou bloqueio declarado por hook: chamada de
    # API DEPOIS da declaração desfaz. Um `idle` vindo do `Stop` é fato do hook e
    # não se toca.
    if estado == BLOCKED or (estado == IDLE and quem in DEDUZIDOS):
        if isinstance(mexeu, (int, float)) and mexeu > desde:
            return "api-ativa"
        return None

    if estado != WORKING:
        return None

    if not isinstance(mexeu, (int, float)):
        return None

    # Silêncio contado a partir do INÍCIO DO TURNO, e não da última chamada de
    # API. Uma sessão que dormiu três horas e acabou de receber um prompt tem
    # `api_ms_ts` de três horas atrás; medindo dali, ela seria declarada parada
    # no mesmo instante em que começou a trabalhar.
    parado = now - max(mexeu, desde)

    # Dentro da carência não se acusa nada. Além de evitar o falso positivo da
    # marca recém-escrita, isto poupa a leitura do transcript no caso normal —
    # uma sessão trabalhando nunca chega aqui.
    if parado < GRACE_SECONDS:
        return None

    caminho = (rec.get("data") or {}).get("transcript_path") \
        or rec.get("transcript_path")
    if transcript.interrompido(caminho):
        return "interrompido"

    if parado > STALL_SECONDS:
        return "parado"

    return None


def corrigir_estado_se_couber(rec, now):
    """`corrigir_estado`, mas so quando o motor de deducao esta valendo.

    Com o herdr mandando, deduzir seria pior do que inutil: as duas fontes
    disputariam o mesmo campo, e a que erra mais teria a ultima palavra por
    chegar depois. `fundir` ja aplica o estado do herdr no caminho de leitura.
    """
    if motor.atual() != motor.HOOKS:
        return None
    return corrigir_estado(rec, now)


def abrir_trabalho(rec, sid, now):
    """Começa um trabalho nesta sessão. Chamado sob _lock.

    Um trabalho anterior ainda aberto aqui significa que o `Stop` dele nunca
    chegou — Esc no meio do turno, queda do Claude Code, POST do hook que
    estourou o prazo. Ele fecha marcado `stalled` em vez de ser abandonado:
    aconteceu, custou dinheiro, e some da conta se for descartado.
    """
    if rec.get("work"):
        fechar_trabalho(rec, now, "stalled")
    rec["work"] = works.abrir(sid, rec.get("data") or {}, now)


def fechar_trabalho(rec, now, motivo=None):
    """Fecha, grava e devolve o registro. None quando não havia trabalho aberto.

    O trabalho sai do registro da sessão SEMPRE, mesmo que a gravação falhe:
    deixá-lo aberto faria o próximo `UserPromptSubmit` fechá-lo de novo, agora
    com a duração errada.
    """
    trab = rec.pop("work", None)
    if not trab:
        return None
    reg = works.fechar(trab, rec.get("data") or {}, now, motivo)
    if reg:
        works.gravar(reg)
    return reg


def prune():
    """Remove sessões mortas. Chamado sob _lock.

    Três regras, da mais confiável para a menos:

    1. SessionEnd  — tratado no /event, remove na hora. É a verdade.
    2. TTL         — só para sessão SEM hooks (instalação antiga). Com hooks,
                     o silêncio deixou de ser sinal de morte: um agente pode
                     ficar horas legitimamente bloqueado esperando você.
    3. PID morto   — último recurso, para queda bruta (janela fechada no X,
                     reboot) em que o SessionEnd nunca dispara.

    A regra 3 NUNCA se aplica a uma sessão `blocked`. Motivo medido: o PID
    publicado pode apontar para um processo filho transitório já morto enquanto
    a sessão está viva e travada numa pergunta. Confiar nele apagaria justamente
    o agente que espera por você.
    """
    now = _now()
    dead = []
    for sid, rec in _sessions.items():
        # O trabalho que ficou pendente e nunca recebeu o heartbeat de
        # fechamento. Cobrado aqui e não por um timer: `prune` já roda sob o
        # lock em toda escrita, e se nada mais acontecer na máquina o trabalho
        # espera — nesse cenário ninguém está olhando o número.
        trab = rec.get("work")
        pend = trab.get("pendente_ts") if trab else None
        if pend and (now - pend) > FECHAMENTO_MAX_S:
            fechar_trabalho(rec, now, "partial")

        quiet = now - max(rec["ts"], rec.get("state_ts") or 0)
        ttl = HOOKED_TTL if rec.get("hooked") else TTL_SECONDS
        if quiet > ttl:
            dead.append(sid)
            continue

        # Só custa uma chamada de sistema, e só para quem já está em silêncio.
        if quiet > DEAD_PID_GRACE and resolve_state(rec, now) != BLOCKED:
            alive = pid_alive(rec.get("pid"))
            rec["proc_alive"] = alive
            if alive is False:
                dead.append(sid)

    for sid in dead:
        # A sessão está saindo com um trabalho aberto: ele nunca vai terminar.
        fechar_trabalho(_sessions[sid], now, "aborted")
        del _sessions[sid]
        # Sem isto, uma sessao que fecha logo depois de um episodio de
        # divergencia deixava a entrada em `_divergencias` para sempre: nada
        # mais a remove, e a API roda como servico persistente, reiniciado so
        # em deploy manual — semanas de sessoes acumulariam uma entrada cada.
        with _div_lock:
            _divergencias.pop(sid, None)

    # Gravar AQUI, e não em quem chama. Antes só /ingest e /event salvavam
    # depois de podar, e são justamente os dois que rodam menos: a poda que
    # acontece durante um GET /status apagava a sessão da memória e a deixava
    # no state.json. No reinício seguinte ela voltava do túmulo — sintoma
    # relatado como "fechei e continua aparecendo".
    if dead:
        save_state(force=True)
    return len(dead)


def uso_do_recorte(registros, recorte, now):
    """A quebra por modelo de um recorte de tempo.

    `recorte` aceita "hoje", "tudo", ou uma data "AAAA-MM-DD". Qualquer outra
    coisa cai em "hoje": um recorte que nao da para entender nao pode virar
    "tudo" por acidente e mostrar meses onde se esperava um dia.

    O `dia` devolvido vira o `dia` que a tabela de preco usa para decidir
    promocao — por isso "tudo" carrega a data de hoje na conta, e so o rotulo
    de saida diz "tudo".
    """
    hoje = time.strftime("%Y-%m-%d", time.localtime(now))
    rotulo = hoje

    if recorte == "tudo":
        regs, dia, rotulo = registros, hoje, "tudo"
    else:
        alvo = None
        if recorte and len(recorte) == 10 and recorte.count("-") == 2:
            try:
                alvo = time.mktime(time.strptime(recorte, "%Y-%m-%d"))
            except ValueError:
                alvo = None
        if alvo is not None:
            regs = [r for r in registros
                    if isinstance(r, dict)
                    and isinstance(r.get("started"), (int, float))
                    and alvo <= r["started"] < alvo + 86400]
            dia = rotulo = recorte
        else:
            regs, dia = works.desde(registros, works.inicio_do_dia(now)), hoje

    fora = works.por_modelo(regs, dia=dia)
    fora["dia"] = rotulo
    return fora


# O vitalício varre o livro-caixa INTEIRO, e o painel pede /status a cada dois
# segundos. `works.ler_cache` já evita reler o arquivo, mas não evita recontar:
# sem esta memória seriam 700+ registros varridos 43 mil vezes por dia para um
# número que só muda quando um turno fecha.
#
# A chave é a IDENTIDADE da lista devolvida pelo cache, e não o tamanho dela:
# `ler_cache` devolve o MESMO objeto enquanto o arquivo não muda, então `is`
# responde exatamente "o livro mudou?" sem nenhuma conta — e sem repetir aqui a
# regra de invalidação que já mora lá.
_vit_cache = {"regs": None, "valor": None}


def vitalicio_do_livro():
    """Totais de toda a história, recalculados só quando o livro-caixa muda."""
    regs = works.ler_cache()
    if _vit_cache["regs"] is not regs:
        _vit_cache["regs"] = regs
        _vit_cache["valor"] = works.vitalicio(regs)
    return _vit_cache["valor"]


# ---- Captura de tela ----
#
# O pedido pega carona no polling que a placa já faz: `POST /tela/pedir` arma um
# id, o `GET /status` seguinte o carrega em `captura`, e a placa devolve o
# framebuffer num `POST /tela`. Um servidor HTTP dentro do firmware seria um GET
# direto e mais simples de chamar, mas custaria RAM, uma porta aberta sem
# autenticação na LAN e um IP fixo para a placa — hoje ela só faz requisições de
# saída.
CAPTURA_TTL = 30

# Teto do corpo do quadro, separado do MAX_BODY dos JSONs: são 307.200 bytes de
# framebuffer contra os 64 KB que bastam a um payload de statusline. Um teto só,
# compartilhado, obrigaria a afrouxar o do JSON — e o JSON é o que esta porta
# aberta na LAN aceita de qualquer um.
TELA_MAX_BODY = 1024 * 1024

# O contador é semeado com o RELÓGIO, e não com zero. O pedido vive em memória
# (o save_state persiste apenas as sessões) e esta API reinicia a cada logon: um
# contador que recomeçasse do zero repetiria ids já usados, e a placa — que
# guarda o último id atendido por fonte — descartaria a captura nova por achar
# que já a atendeu.
_captura_seq = int(time.time())

# O pedido pendente e a última captura gravada. Sob `_lock`, o mesmo das
# sessões, respeitando a ordem de aquisição documentada em `_div_lock`.
_captura = {
    "pedido": None,     # {"id": int, "ts": float} — no máximo UM, o mais recente
    "ultima": None,     # {"id": int, "arquivo": str, "ts": float}
}


def _pedido_vivo(pedido, now):
    """O pedido que ainda vale. `None` quando não há, ou quando venceu.

    O vencido NÃO é apagado: é ele que faz o `GET /tela` responder `expirado` em
    vez de `vazio`, que é a diferença entre "a placa não atendeu" e "você nunca
    pediu nada".
    """
    if not pedido or (now - pedido["ts"]) > CAPTURA_TTL:
        return None
    return pedido


def parse_tela(cabecalho):
    """`id=1; origem=0; w=320; h=480; rot=1; fmt=raw` -> dict de strings."""
    fora = {}
    for parte in (cabecalho or "").split(";"):
        if "=" in parte:
            chave, valor = parte.split("=", 1)
            fora[chave.strip()] = valor.strip()
    return fora


def pedir_captura(now):
    """Arma um pedido de captura e devolve o corpo da resposta.

    Um pedido novo SUBSTITUI o pendente: dois `/pedir` em sequência deixam um só,
    com o id maior. Guardar os dois faria a placa mandar duas telas quase iguais
    e a segunda gravaria por cima da primeira.

    O TTL existe para o caso em que a placa está fora do ar. Sem ele o pedido
    ficaria armado para o próximo boot dela, e a imagem chegaria descrevendo uma
    tela que ninguém pediu.
    """
    global _captura_seq
    with _lock:
        _captura_seq += 1
        _captura["pedido"] = {"id": _captura_seq, "ts": now}
        return {"id": _captura_seq, "ttl": CAPTURA_TTL}


def receber_captura(cabecalho, corpo, now):
    """O quadro chegou. Devolve `(código HTTP, corpo da resposta)`.

    O TAMANHO é conferido ANTES do id, e a ordem é a informação: um corpo
    truncado é defeito do envio, e responder `409` ali mandaria procurar o
    problema em sincronia de pedidos, que é o lugar errado.

    O PNG é escrito FORA da trava. São ~300 KB passando por zlib, e segurar
    `_lock` durante isso pararia o `/status` que a placa pede a cada dois
    segundos — para gravar um diagnóstico. O pedido é conferido antes e
    reconferido depois: um `/pedir` que chegue no meio da gravação continua
    armado, e a placa o atende na volta seguinte.
    """
    cab = parse_tela(cabecalho)
    try:
        ident = int(cab["id"])
        w = int(cab["w"])
        h = int(cab["h"])
        rot = int(cab.get("rot", 1))
    except (KeyError, TypeError, ValueError):
        return 400, {"error": "cabecalho X-Tela invalido", "X-Tela": cabecalho}

    # `fmt` só é recusado quando vem DIFERENTE de `raw`: ausente é o formato de
    # sempre, mas um formato novo decodificado como cru viraria chuvisco em vez
    # de erro — e chuvisco numa captura passa por defeito do firmware.
    if cab.get("fmt", "raw") != "raw":
        return 400, {"error": "formato nao suportado", "fmt": cab.get("fmt")}

    if not tela.confere(corpo, w, h):
        return 400, {"error": "corpo nao tem w*h*2 bytes",
                     "esperado": w * h * 2, "recebido": len(corpo)}

    with _lock:
        pedido = _pedido_vivo(_captura["pedido"], now)
    if not pedido or pedido["id"] != ident:
        # O caso real é o segundo `/pedir` chegar enquanto a placa já estava
        # enviando o primeiro. Gravar assim mesmo entregaria, para o pedido novo,
        # a tela de antes dele existir.
        return 409, {"error": "id nao e o do pedido pendente", "id": ident,
                     "pendente": pedido["id"] if pedido else None}

    arquivo = tela.gravar(corpo, w, h, rot, agora=now)
    if not arquivo:
        return 500, {"error": "nao deu para gravar o PNG"}

    with _lock:
        if (_captura["pedido"] or {}).get("id") == ident:
            _captura["pedido"] = None
        _captura["ultima"] = {"id": ident, "arquivo": arquivo, "ts": now}
    return 200, {"ok": True, "arquivo": arquivo, "bytes": len(corpo)}


def estado_captura(now):
    """Em que pé está a captura. É o que o disparador consulta enquanto espera.

    O pedido pendente vem ANTES da última captura: com um pedido no ar, dizer
    `pronto` entregaria ao disparador a imagem anterior, e ele pararia de esperar
    com a tela errada na mão.
    """
    with _lock:
        pedido = _captura["pedido"]
        ultima = _captura["ultima"]

    if pedido:
        vivo = _pedido_vivo(pedido, now)
        return {"estado": "pendente" if vivo else "expirado",
                "arquivo": None, "id": pedido["id"],
                "idade_s": int(now - pedido["ts"])}
    if ultima:
        return {"estado": "pronto", "arquivo": ultima["arquivo"],
                "id": ultima["id"], "idade_s": int(now - ultima["ts"])}
    return {"estado": "vazio", "arquivo": None, "id": None, "idade_s": None}


def build_status():
    """Agrega as sessões vivas num payload plano."""
    now = _now()
    with _lock:
        prune()
        live_items = list(_sessions.items())   # precisa do id para identificar o agente
        live = [rec for _, rec in live_items]
        pedido = _pedido_vivo(_captura["pedido"], now)

    # O pedido de captura viaja de carona nesta resposta: é o único caminho até
    # a placa, que só faz requisições de saída. `null` no caso normal — e ele
    # PRECISA sair nos dois retornos desta função (ver o comentário do
    # `bloqueio` no retorno sem sessão viva): sem sessão aberta é justamente
    # quando se ajusta relógio, clima e cabeçalho, ou seja, quando mais se pede
    # captura.
    captura = {"id": pedido["id"]} if pedido else None

    # O pedido de arquivo para o cartao viaja pela mesma carona, pela mesma
    # razao, e tambem PRECISA sair nos dois retornos: atualizar sprite e coisa
    # que se faz de madrugada, sem sessao nenhuma aberta.
    arq = arquivo.para_status(now)

    # UM retrato só, para toda a resposta: a thread de fundo do herdr reescreve
    # o dele a cada 2s, e duas chamadas separadas a herdr.snapshot() nesta
    # mesma resposta podiam pegar dois instantes diferentes — labels fundido
    # com um retrato online e herdr_online:false no topo, ou o inverso.
    retrato = herdr.snapshot()

    if not live:
        # Zero sessoes NOSSAS nao quer dizer zero agentes: o herdr pode estar
        # vendo um codex, gemini ou cursor trabalhando sem Claude Code nenhum
        # aberto. Chamar build_agents([], retrato) e o que faz esse agente
        # aparecer em `labels` — sem isto, o payload saia com `herdr_online:
        # true` e `labels: []` ao mesmo tempo, e o firmware desenhava "CLAUDIO
        # OFFLINE" justamente no caso para o qual este sensor foi escrito.
        # `online` continua False: ele significa "alguem do Claude Code
        # publicando agora", e essa semantica nao muda so porque o herdr ve
        # outra coisa.
        agents = build_agents([], retrato)
        return {
            "online": False,
            "sessions": 0,
            "sessions_active": 0,
            "blocked": sum(1 for a in agents if a["state"] == BLOCKED),
            "model": None,
            # Ver o comentario do mesmo campo no outro retorno. Sai NOS DOIS: o
            # plano e da conta, e a conta existe mesmo sem nenhuma sessao aberta
            # — publicar so no outro caminho apagaria o plano da tela justamente
            # quando o painel vira relogio de parede.
            "planos": build_planos(),
            "effort": None,
            "context_pct": 0,
            "context_repo": None,
            "context_branch": None,
            "session_pct": 0,
            "session_resets_in": 0,
            "session_resets_hm": "-",
            "session_resets_clock": "-",
            "week_pct": 0,
            "week_resets_in": 0,
            "week_resets_dh": "-",
            "week_resets_date": "-",
            "updated_ago": 0,
            "limits_fresh": False,
            "session_known": False,
            "week_known": False,
            "session_inferred": False,
            "session_label": "Session —",
            "week_label": "Week —",
            "labels": agents,              # so nao vazio quando o herdr ve algo
            # Sem sessao NOSSA ainda pode haver bloqueado: um codex ou um
            # gemini que o herdr enxerga e nos nao. Omitir aqui esconderia a
            # pergunta justamente do agente que a nossa API nao acompanha.
            "bloqueio": bloqueio.atual(agents),
            # Ver o comentário de `captura` lá em cima. Sem sessão viva o painel
            # continua desenhando, então continua havendo tela para capturar.
            "captura": captura,
            "arquivo": arq,
            "colors": {"context": "green", "session": "green", "week": "green"},
            "herdr_online": retrato["online"],
            "done": sum(1 for a in agents if a.get("done")),
            # Sem sessao nossa nao ha limpeza nossa para relatar.
            "cleaning": False,
            # O livro-caixa nao depende de haver sessao viva: o dia ja teve os
            # turnos que teve, e some-los e o unico jeito de a tela nao zerar
            # quando voce fecha o ultimo terminal.
            "works": works.resumo(
                works.desde(works.ler_cache(), works.inicio_do_dia(now)), now),
            # A quebra por modelo do DIA. Vai junto do `works` porque responde
            # a mesma pergunta por outro eixo: `works` diz quanto o dia custou,
            # `uso` diz de QUEM foi o custo. E vai no /status, e nao num
            # endpoint proprio, porque o painel ja pede este endereco a cada
            # dois segundos.
            "uso": uso_do_recorte(works.ler_cache(), "hoje", now),
            # A vida inteira, que alimenta o nível do Clawd na quarta tela.
            # `works` fala do dia e `uso` de quem gastou o dia; este fala de
            # tudo. Memoizado: ver `vitalicio_do_livro`.
            "vitalicio": vitalicio_do_livro(),
            # Qual motor decidiu o estado agora. Ver `corrigir_estado_se_couber`.
            "motor": motor.atual(),
            # Quem esta falando. Ver `tag_da_maquina`.
            "tag": TAG,
            # Data, hora e tempo vao TAMBEM aqui: sem nenhuma sessao viva o
            # painel continua sendo um relogio na parede. Omitir seria apagar o
            # cabecalho justamente quando nao ha mais nada para mostrar.
            **weather.snapshot(),
        }

    # `online` = alguém publicando AGORA. Agentes ociosos continuam na lista,
    # mas não sustentam sozinhos a afirmação "o Claude Code está ativo".
    fresh_recs = [rec for rec in live if (now - rec["ts"]) <= FRESH_SECONDS]
    online = bool(fresh_recs)

    # O resumo do topo sai só de quem está publicando: um número de 10 minutos
    # atrás não pode virar a manchete. Sem ninguém fresco, usa o que existe
    # para a tela não zerar — `online: false` já avisa que está velho.
    base = fresh_recs or live

    # Sessão mais recente: define modelo. Sessão de maior context: define repo/branch.
    newest = max(base, key=lambda r: r["ts"])
    with_ctx = [r for r in base if isinstance(r["data"].get("context_pct"), (int, float))]
    top_ctx = max(with_ctx, key=lambda r: r["data"]["context_pct"]) if with_ctx else newest

    # --- Rate limits ---
    # NÃO são "globais e iguais em toda sessão". Cada sessão carrega um SNAPSHOT
    # congelado da sua última chamada de API; o heartbeat republica o snapshot
    # velho sem atualizá-lo. Sessões ociosas há horas carregam lixo de janelas
    # já expiradas (visto na prática: snapshot de 51h atrás).
    #
    # Descartar janela expirada (resets_at no passado) elimina o lixo antigo, mas
    # NÃO basta: `resets_at` é o fim da janela de 5h, portanto é IGUAL para toda
    # sessão dentro da mesma janela. Duas sessões que falaram com a API às 14h05 e
    # às 18h15 reportam o mesmo resets_at com percentuais diferentes — ordenar por
    # ele empata, e o desempate por timestamp de publicação vira sorteio (todas
    # republicam a cada 10s pelo heartbeat).
    #
    # Regra correta, em duas chaves:
    #   1) maior `session_resets_at` -> a janela de 5h vigente;
    #   2) dentro dela, maior `session_pct` -> o snapshot mais recente.
    #
    # (2) vale porque a janela de 5h é FIXA (reseta num horário, não desliza):
    # dentro dela o uso só CRESCE. Então o maior percentual é, por definição, a
    # leitura mais nova. Isso dispensa adivinhar quem chamou a API por último.
    #
    # Cuidado: aplicar max(pct) ao percentual SEMANAL seria errado — a janela de
    # 7 dias é deslizante e o valor pode CAIR (uso antigo sai da janela). Por isso
    # elegemos a SESSÃO pelo critério da janela de 5h e usamos o snapshot INTEIRO
    # dela (session + week), que é internamente coerente.
    #
    # `valid_reset` e não `> now`: sem o teto, uma sessão com carimbo absurdo é
    # sempre a de maior `session_resets_at` e sequestra os limites da conta
    # inteira (ver SESSION_HORIZON).
    # UMA JANELA NÃO DERRUBA A OUTRA.
    janela_5h = pick_window(live, "session_resets_at", "session_pct",
                            SESSION_HORIZON, now)
    janela_7d = pick_window(live, "week_resets_at", "week_pct",
                            WEEK_HORIZON, now)

    session_inferred = False
    if janela_5h:
        session_pct, session_resets_at = janela_5h
        session_known = True
    elif expired_window_seen(live, "session_resets_at", now):
        # A JANELA VIROU, e isso é um fato, não uma lacuna.
        #
        # A janela de 5h é fixa: quando ela vence, o uso recomeça do zero.
        # Confirmado por observação, não deduzido — no instante da virada uma
        # sessão publicou `session_pct: 0` por conta própria enquanto as outras
        # ainda carregavam 32% e 40% da janela anterior.
        #
        # Antes disto, o intervalo entre "a janela venceu" e "alguém chamar a
        # API de novo" mostrava "—". Esse intervalo dura até você mandar uma
        # mensagem, ou seja, pode durar horas — e esconde justamente o fato mais
        # acionável que existe: você está com a cota inteira.
        session_pct = 0
        session_resets_at = 0        # o PRAZO segue desconhecido (ver abaixo)
        session_known = True
        session_inferred = True
    else:
        # Nunca houve carimbo nenhum: não há janela para ter virado.
        session_pct = 0
        session_resets_at = 0
        session_known = False

    if janela_7d:
        week_pct, week_resets_at = janela_7d
        week_known = True
    else:
        # A janela de 7 dias DESLIZA — ela não "vira" com uso zerado, então o
        # truque acima não vale aqui. Sem carimbo válido, não se sabe.
        week_pct = 0
        week_resets_at = 0
        week_known = False

    # Zero por si só significaria "reseta agora". O prazo da janela nova só
    # existe depois que alguém usar, e inventar um horário seria a mentira que
    # o percentual zero não é.
    session_in = int(session_resets_at - now) if session_resets_at else 0
    week_in = int(week_resets_at - now) if week_resets_at else 0

    # Mantido para quem lê o campo antigo: só é verdade quando as DUAS janelas
    # vêm de leitura real.
    limits_fresh = session_known and week_known and not session_inferred

    model = short_model(newest["data"].get("model"))
    effort = effort_label(newest["data"].get("effort"))
    ctx_pct = int(round(top_ctx["data"].get("context_pct") or 0))
    ctx_repo = top_ctx["data"].get("repo")
    ctx_branch = top_ctx["data"].get("branch")
    sess_pct = int(round(session_pct))
    wk_pct = int(round(week_pct))
    # "-" e nao "0h00m": zero leria como "reseta agora", que e diferente de
    # "nao sei quando reseta".
    sess_hm = fmt_hm(session_in) if session_resets_at else "-"
    wk_dh = fmt_dh(week_in) if week_resets_at else "-"
    # O MESMO carimbo, dito da outra forma. Sem prazo nao ha momento: "-" pelo
    # mesmo motivo de cima — zero leria como "reseta agora".
    sess_at = fmt_clock(session_resets_at) if session_resets_at else "-"
    wk_at = fmt_date(week_resets_at) if week_resets_at else "-"
    sess_label, wk_label = limits_labels(sess_pct, sess_hm, wk_pct, wk_dh,
                                         session_known, week_known)

    agents = build_agents(live_items, retrato)
    return {
        "online": online,
        "sessions": len(live),          # inclui os ociosos: eles estao vivos
        "sessions_active": len(fresh_recs),
        # Quantos precisam de você AGORA. Fica no topo para que o painel possa
        # avisar mesmo na página que não mostra a lista de agentes.
        "blocked": sum(1 for a in agents if a["state"] == BLOCKED),
        "model": model,
        # O plano de cada conta. GLOBAL, e nao por agente: `labels[]` descreve
        # sessoes, e o plano descreve a assinatura por tras delas. Provedor sem
        # plano conhecido nao aparece aqui, e a placa desenha "—".
        "planos": build_planos(),
        "effort": effort,
        "context_pct": ctx_pct,
        "context_repo": ctx_repo,
        "context_branch": ctx_branch,
        "session_pct": sess_pct,
        "session_resets_in": max(0, session_in),
        "session_resets_hm": sess_hm,
        # A hora do relogio em que a janela de 5h vira, e a data em que a de 7
        # dias vira. Campos novos: uma placa antiga simplesmente nao os le.
        "session_resets_clock": sess_at,
        "week_pct": wk_pct,
        "week_resets_in": max(0, week_in),
        "week_resets_dh": wk_dh,
        "week_resets_date": wk_at,
        "updated_ago": int(now - newest["ts"]),
        "limits_fresh": limits_fresh,
        # Uma flag por janela: elas expiram em ritmos diferentes e uma vencida
        # nao pode mais apagar a outra.
        "session_known": session_known,
        "week_known": week_known,
        # O percentual veio da virada da janela, e nao de leitura. O numero e
        # confiavel; o PRAZO e que nao existe ainda.
        "session_inferred": session_inferred,
        # Limites sao da CONTA: ficam no topo, fora da lista de agentes.
        "session_label": sess_label,
        "week_label": wk_label,
        # Um agente por entrada: bloqueados primeiro, depois maior contexto.
        "labels": agents,
        # A pergunta que trava o primeiro bloqueado da lista, se houver — com as
        # opcoes ja detectadas na tela dele. `null` quando ninguem esta
        # bloqueado, e nesse caso nada e lido. Ver bloqueio.atual.
        "bloqueio": bloqueio.atual(agents),
        # O pedido de captura pendente, se houver. Ver `captura` lá em cima.
        "captura": captura,
        "arquivo": arq,
        "colors": {
            "context": pct_color(ctx_pct),
            "session": pct_color(sess_pct),
            "week": pct_color(wk_pct),
        },
        "herdr_online": retrato["online"],
        "done": sum(1 for a in agents if a.get("done")),
        # Alguem compactando contexto agora, por ordem dada do painel. Um
        # booleano so, e nao por agente: quem varre e a turma do rodape, que e
        # global. Ver `limpando`.
        "cleaning": any(limpando(rec, now) for rec in live),
        # O dia, vindo do livro-caixa. As metricas por agente falam da SESSAO,
        # que e cumulativa desde que ela abriu; isto fala do DIA, somando todas
        # as sessoes que ja terminaram turnos. Sao perguntas diferentes.
        #
        # `ler_cache` so rele quando o arquivo muda, entao o painel pedindo
        # /status a cada dois segundos nao relê 600 linhas por pedido.
        "works": works.resumo(
            works.desde(works.ler_cache(), works.inicio_do_dia(now)), now),
        # Ver o comentario do mesmo campo no retorno sem sessao viva.
        "uso": uso_do_recorte(works.ler_cache(), "hoje", now),
        # Ver o comentario do mesmo campo no outro retorno.
        "vitalicio": vitalicio_do_livro(),
        # Qual motor decidiu o estado agora. Ver `corrigir_estado_se_couber`.
        "motor": motor.atual(),
        # Quem esta falando: a tag desta maquina, que o painel mostra no chip de
        # cada agente quando ha mais de uma fonte. Ver `tag_da_maquina`.
        "tag": TAG,
        # `clock` e `weather`: o mundo fora do Claude Code. Entram no mesmo
        # payload porque o painel ja pede este endereco a cada dois segundos —
        # uma segunda requisicao so para a hora seria trabalho duplicado.
        **weather.snapshot(),
    }


def build_health():
    """O que responder em /health. Funcao separada do handler para ter teste."""
    return {
        "ok": True,
        "ttl": TTL_SECONDS,
        "port": PORT,
        # A tag desta maquina, aqui tambem: e por /health que se confere a
        # instalacao de um slave antes de haver sessao nenhuma publicando.
        "tag": TAG,
        # Estado do sensor do herdr: achou o binario? esta online agora? ha
        # quanto tempo veio o ultimo retrato? Util porque o `print()` de boot
        # e o de divergencia nao chegam a lugar nenhum sob pythonw.exe
        # destacado (sys.stdout is None) — isto e o que sobra para
        # diagnosticar de fora.
        "herdr": herdr.status(),
        # Qual motor decide o estado, e desde quando. Sem isto, "desde quando"
        # so existiria num print que ninguem le: sob pythonw o sys.stdout e None.
        "motor": motor.status(),
        # Ultimas vezes em que o herdr e a nossa maquina de estados
        # discordaram por mais de DIVERGENCIA_S. Mesmo motivo: o print
        # de fundir() nao escreve nada em producao.
        "divergencias": list(_divergencia_log),
        # Quantas vezes a sonda de modelo do orfao levantou desde o boot. Zero
        # e o esperado — modelos.py promete nao levantar. Diferente de zero e o
        # unico jeito de separar "a sonda quebrou" de "nao achei nada": os dois
        # viram None e desenham o mesmo "—" na tela. Ver `_sondas_falhas`.
        "sondas_falhas": _sondas_falhas,
    }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass  # silencioso — roda em background

    def _send(self, code, payload):
        # ensure_ascii=False: emite UTF-8 real (❖ em vez de ❖) — menor e legível.
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0].rstrip("/") or "/"
        if path in ("/status", "/"):
            # `?campos=painel` corta o que o firmware nao le (ver painel.py).
            # Quem pede e a PLACA: sem o parametro o documento sai inteiro, que
            # e o contrato de tools/tela.py, do curl de depuracao e de qualquer
            # placa gravada antes desta versao.
            corpo = build_status()
            if painel.pedido_do_painel(self.path):
                corpo = painel.enxugar(corpo)
                # `works`, `uso` e `vitalicio` mudam quando um turno termina, e
                # nao a cada dois segundos. O resumo sai do documento JA enxuto:
                # calculado antes, ele nunca bateria com o que a placa recebeu.
                corpo = painel.aplicar_frio(corpo, self.path)
            self._send(200, corpo)
        elif path == "/health":
            self._send(200, build_health())
        elif path == "/sessions":
            with _lock:
                prune()
                now = _now()
                detail = [
                    {
                        "session_id": sid,
                        "age": int(now - rec["ts"]),
                        "limits_age": int(now - rec.get("limits_ts", now)),
                        "state": resolve_state(rec, now),
                        "state_raw": rec.get("state"),
                        "state_age": int(now - rec["state_ts"]) if rec.get("state_ts") else None,
                        "event": rec.get("event"),
                        "kind": rec.get("kind"),
                        "message": rec.get("message"),
                        "hooked": bool(rec.get("hooked")),
                        "proc_alive": pid_alive(rec.get("pid")),
                        **rec["data"],
                    }
                    for sid, rec in _sessions.items()
                ]
            self._send(200, {"sessions": detail})
        elif path == "/uso":
            # ?dia=hoje (default) | AAAA-MM-DD | tudo
            query = {}
            if "?" in self.path:
                for par in self.path.split("?", 1)[1].split("&"):
                    if "=" in par:
                        k, v = par.split("=", 1)
                        query[k] = v
            self._send(200, uso_do_recorte(works.ler_cache(),
                                           query.get("dia", "hoje"), _now()))
        elif path == "/tela":
            # Em que pé está a captura pedida. Quem pergunta é o disparador
            # (`tools/tela.py`) enquanto espera o quadro chegar — a placa nunca
            # lê esta rota, ela só POSTa.
            self._send(200, estado_captura(_now()))
        elif path == "/arquivo":
            # Em que pé está a atualização de arquivo. Quem pergunta é o
            # disparador (`tools/atualizar_sprite.py`) enquanto espera.
            self._send(200, arquivo.estado(_now()))
        elif path == "/arquivo/baixar":
            # O binário do pedido pendente — a única rota que a PLACA chama
            # neste fluxo além do /status. Serve apenas o arquivo do pedido
            # vivo: nome nenhum viaja na URL.
            code, corpo = arquivo.corpo_para_baixar(_now())
            if code == 200:
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(corpo)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(corpo)
            else:
                self._send(code, corpo)
        elif path == "/terminal":
            # ?pane_id=w0:p1&cols=78&rows=34&rolagem=0
            #
            # `pane_id` VAZIO fecha a tela: e assim que o painel devolve o
            # terminal ao tamanho original. Ver terminal.soltar.
            #
            # Somente LEITURA da tela de um agente. Nao ha aqui nada que escreva
            # no pane: o painel abre esta tela para LER, e a unica rota que age
            # sobre um agente continua sendo /responder, com as conferencias
            # dela. Uma tela de terminal que tambem digitasse seria, nesta API
            # sem autenticacao, o teclado aberto na rede que a lista branca de
            # comandos existe para impedir.
            query = {}
            if "?" in self.path:
                for par in self.path.split("?", 1)[1].split("&"):
                    if "=" in par:
                        k, v = par.split("=", 1)
                        query[k] = unquote(v)
            try:
                self._send(200, terminal.ler(query.get("pane_id", ""),
                                             query.get("cols"),
                                             query.get("rows"),
                                             query.get("rolagem")))
            except (TypeError, ValueError):
                # Parametro que nao e numero vem da rede, entao vira 400 e nao
                # exceção: um traceback aqui derrubaria a thread do pedido e o
                # painel veria a conexao cair sem explicacao.
                self._send(400, {"error": "cols, rows e rolagem precisam ser numeros"})
        else:
            self._send(404, {"error": "not found"})

    def _read_json(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_BODY:
            raise ValueError("bad length")
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def _read_bytes(self, teto):
        """O corpo cru, sem passar por JSON. Levanta quando não cabe no teto.

        Quem recusa por tamanho FECHA a conexão: em HTTP/1.1 ela é reaproveitada,
        e um corpo não lido continuaria no socket para ser interpretado como a
        próxima requisição.
        """
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > teto:
            self.close_connection = True
            raise ValueError("bad length")
        return self.rfile.read(length)

    def _handle_command(self, data):
        """Dispara um comando na sessao. So o que esta em COMANDOS."""
        sid = data.get("session_id") or ""
        nome = data.get("cmd") or ""
        texto = COMANDOS.get(nome)
        if not texto:
            self._send(400, {"error": "comando nao permitido",
                             "permitidos": sorted(COMANDOS)})
            return

        with _lock:
            chave = resolver_sessao(_sessions, sid)
            rec = _sessions.get(chave) or {}
            pane = rec.get("herdr_pane_id")
        if not pane:
            # Sem par no herdr nao ha para onde escrever. Dizer isso e melhor do
            # que escrever no pane errado.
            self._send(404, {"error": "sessao sem pane do herdr", "session_id": sid})
            return

        ok = herdr.enviar(pane, texto)

        # Acende a limpeza SO quando o herdr aceitou. Acender antes deixaria a
        # turma varrendo por causa de um comando que nunca chegou a lugar nenhum.
        if ok and nome == "compact":
            with _lock:
                r = _sessions.get(chave)
                if r is not None:
                    r["limpeza_ts"] = time.time()
                    # O contexto de ANTES: e a queda dele que prova o fim.
                    r["limpeza_ctx"] = (r.get("data") or {}).get("context_pct")

        # `ok` falso nao prova que nada aconteceu — ver herdr.enviar. Por isso a
        # resposta diz "enviado", e nao "executado".
        self._send(200 if ok else 502,
                   {"enviado": ok, "pane": pane, "cmd": nome, "texto": texto,
                    "session_id": chave})

    def _handle_responder(self, data):
        """Responde o prompt de aprovacao de um agente bloqueado.

        NAO aceita texto nem tecla: so o NUMERO de uma opcao que o proprio
        agente esta oferecendo na tela, mais o rotulo que estava escrito nela na
        hora do toque. O conjunto de acoes possiveis fica limitado ao que o
        agente ja perguntou, e o rotulo garante que a resposta e daquela
        pergunta e nao da seguinte.

        Isto amplia o que alguem na LAN alcanca, e vale dizer com todas as
        letras: de "compactar sua sessao" para "aprovar o que o agente estiver
        perguntando agora". A conferencia de rotulo impede resposta as cegas e
        resposta atrasada; nao impede quem leia o /status (aberto) e responda de
        proposito. Fechar isso exige segredo compartilhado entre painel e API.
        """
        pane = data.get("pane_id") or ""
        rotulo = data.get("rotulo") or ""
        n = data.get("n")
        if not pane or not isinstance(n, int) or not rotulo:
            self._send(400, {"error": "pane_id, n e rotulo sao obrigatorios"})
            return

        ok = bloqueio.responder(pane, n, rotulo)
        # Como em /command, `enviado` e nao `executado`: um timeout depois de o
        # herdr aceitar deixa o efeito feito e nos sem saber.
        self._send(200 if ok else 409,
                   {"enviado": ok, "pane": pane, "n": n, "rotulo": rotulo})

    def do_POST(self):
        path = self.path.split("?")[0].rstrip("/") or "/"
        if path == "/responder":
            try:
                self._handle_responder(self._read_json())
            except Exception:
                self._send(400, {"error": "bad json"})
            return
        if path == "/command":
            try:
                self._handle_command(self._read_json())
            except Exception:
                self._send(400, {"error": "bad json"})
            return
        if path == "/tela/pedir":
            # Corpo VAZIO, e por isso não passa por `_read_json` — ele recusa
            # `length <= 0`. Não há nada a mandar: o pedido é o pedido.
            self._send(200, pedir_captura(_now()))
            return
        if path == "/arquivo/subir":
            # O binario que vai para a staging. Nome no header X-Arquivo
            # (`nome=...`), corpo cru — o mesmo desenho do POST /tela, e com um
            # teto proprio pelo mesmo motivo de la.
            try:
                corpo = self._read_bytes(arquivo.MAX_BYTES)
            except ValueError:
                self._send(400, {"error": "corpo ausente ou grande demais",
                                 "teto": arquivo.MAX_BYTES})
                return
            cab = parse_tela(self.headers.get("X-Arquivo") or "")
            self._send(*arquivo.receber_upload(cab.get("nome"), corpo))
            return
        if path == "/arquivo/pedir":
            try:
                self._send(*arquivo.pedir(self._read_json(), _now()))
            except Exception:
                self._send(400, {"error": "bad json"})
            return
        if path == "/arquivo/ok":
            try:
                self._send(*arquivo.receber_ok(self._read_json(), _now()))
            except Exception:
                self._send(400, {"error": "bad json"})
            return
        if path == "/tela":
            try:
                corpo = self._read_bytes(TELA_MAX_BODY)
            except ValueError:
                self._send(400, {"error": "corpo ausente ou grande demais",
                                 "teto": TELA_MAX_BODY})
                return
            self._send(*receber_captura(self.headers.get("X-Tela") or "",
                                        corpo, _now()))
            return
        if path not in ("/ingest", "/event"):
            self._send(404, {"error": "not found"})
            return
        try:
            data = self._read_json()
        except Exception:
            self._send(400, {"error": "bad json"})
            return

        if path == "/event":
            self._handle_event(data)
            return

        sid = data.get("session_id") or "unknown"
        now = _now()
        # Impressão digital só dos campos de limite. Se mudou, esta sessão acabou
        # de receber dados frescos da API — é ela quem sabe a verdade agora.
        fp = (
            data.get("session_pct"),
            data.get("session_resets_at"),
            data.get("week_pct"),
            data.get("week_resets_at"),
        )
        with _lock:
            # Atualiza no lugar: o heartbeat traz NÚMEROS e não pode apagar o
            # ESTADO que os hooks estabeleceram.
            rec = _sessions.setdefault(sid, {})
            if rec.get("limits_fp") != fp:
                rec["limits_ts"] = now       # mudou (ou é nova): carimba agora
            rec.setdefault("limits_ts", now)
            rec["limits_fp"] = fp
            rec["data"] = data
            rec["ts"] = now
            if data.get("pid"):
                rec["pid"] = data["pid"]
            if data.get("herdr_pane_id"):
                # Mesma chave que o hook ja grava (_handle_event), por um
                # canal mais frequente: a statusline publica a cada 10s contra
                # so um evento de ciclo de vida, entao uma sessao parada pareia
                # bem mais cedo. Nao e heuristica nova nenhuma — e o mesmo
                # HERDR_PANE_ID, disponivel aqui porque statusline.cjs tambem
                # roda dentro do pane do herdr.
                rec["herdr_pane_id"] = data["herdr_pane_id"]
            rec.setdefault("state_ts", 0)

            # `api_ms` é cumulativo e só anda com chamada de API. Guardamos o
            # valor e QUANDO ele mudou pela última vez — é esse instante, e não o
            # do heartbeat, que diz há quanto tempo a sessão não produz nada.
            api = data.get("api_ms")
            if isinstance(api, (int, float)):
                if rec.get("api_ms") != api:
                    rec["api_ms"] = api
                    rec["api_ms_ts"] = now
                rec.setdefault("api_ms_ts", now)

            # Os hooks decidem o estado; aqui ele só é conferido contra o que a
            # máquina mostra, nas duas arestas em que não existe hook.
            quem = corrigir_estado_se_couber(rec, now)
            if quem:
                rec["state"] = WORKING if quem == "api-ativa" else IDLE
                rec["state_ts"] = now
                # Nome que não pode ser confundido com hook nenhum: quem lê o
                # `/status` precisa saber que esta transição foi deduzida aqui.
                rec["event"] = quem

            # ---- Livro-caixa ----
            # Ordem importa. A atividade mede o vão contra o heartbeat ANTERIOR,
            # então tem que ser lida antes de qualquer coisa fechar o trabalho.
            trab = rec.get("work")
            if trab:
                # Sessão nova: o prompt chegou antes do primeiro heartbeat e o
                # trabalho abriu sem base nenhuma. Este é o primeiro contador
                # que existe — sem isto o turno de abertura de toda sessão sai
                # com custo, linhas e tempo de API nulos.
                works.semear(trab, data)
                works.atividade(trab, rec.get("api_ms"), now)
                # `api_ms` ter andado desde o instante do bloqueio é a única
                # prova de que você respondeu: não existe hook de "desbloqueou".
                works.desbloquear(trab, rec.get("api_ms"), now)

                if quem == "interrompido":
                    fechar_trabalho(rec, now, "interrupted")
                elif quem == "parado":
                    fechar_trabalho(rec, now, "stalled")
                elif trab.get("pendente_ts"):
                    # O fim normal: o `Stop` marcou e este heartbeat traz os
                    # contadores completos, inclusive a cauda do turno.
                    fechar_trabalho(rec, now)

            prune()
            save_state()
            count = len(_sessions)
        self._send(200, {"ok": True, "sessions": count})

    def _handle_event(self, data):
        """Hook de ciclo de vida: a fonte autoritativa de ESTADO."""
        sid = data.get("session_id") or "unknown"
        state = data.get("state")
        now = _now()

        with _lock:
            if state == "closed":
                # SessionEnd. Não há ambiguidade: a sessão acabou. Fora da lista
                # imediatamente, sem esperar TTL nenhum.
                #
                # Um trabalho aberto morre junto e entra marcado `aborted`. O
                # `Stop` do último turno pode nem ter chegado — fechar a janela
                # no meio de uma resposta é exatamente esse caso.
                morta = _sessions.get(sid)
                if morta is not None:
                    fechar_trabalho(morta, now, "aborted")
                gone = _sessions.pop(sid, None) is not None
                # Mesmo motivo do prune(): sem isto, fechar bem depois de um
                # episodio de divergencia deixava a entrada orfa para sempre.
                with _div_lock:
                    _divergencias.pop(sid, None)
                save_state(force=True)
                count = len(_sessions)
            else:
                rec = _sessions.setdefault(sid, {"data": {}, "ts": now})
                rec["state"] = state
                rec["state_ts"] = now
                rec["event"] = data.get("event")
                rec["kind"] = data.get("kind")
                rec["message"] = data.get("message")
                rec["hooked"] = True
                if data.get("pid"):
                    rec["pid"] = data["pid"]
                if data.get("transcript_path"):
                    rec["transcript_path"] = data["transcript_path"]
                if data.get("herdr_pane_id"):
                    rec["herdr_pane_id"] = data["herdr_pane_id"]
                # Marca o `api_ms` do instante em que travou. Se ele passar disso
                # depois, houve chamada de API — ou seja, o Claude voltou a
                # trabalhar. Ver corrigir_estado().
                #
                # Substituiu `state_ctx`, que guardava o percentual de contexto:
                # aquele número é inteiro e, com janela de 1M, um ponto são
                # 10.000 tokens — grosso demais para enxergar a retomada.
                # Nada a guardar: a reversão compara `api_ms_ts` com `state_ts`,
                # e os dois já existem.
                rec.setdefault("limits_ts", now)

                # ---- Livro-caixa ----
                # Um trabalho vai do UserPromptSubmit ao Stop, e o Stop apenas
                # MARCA: quem fecha é o heartbeat seguinte, com os contadores
                # completos. Ver FECHAMENTO_MAX_S.
                evento = data.get("event")
                if evento == "UserPromptSubmit":
                    abrir_trabalho(rec, sid, now)
                elif evento == "Stop":
                    trab = rec.get("work")
                    if trab:
                        trab["pendente_ts"] = now
                if state == BLOCKED:
                    # Conta o bloqueio já na abertura dele. Contado no
                    # desbloqueio, um bloqueio que você nunca respondeu sumiria
                    # do registro — o caso mais interessante de todos.
                    works.bloquear(rec.get("work"), rec.get("api_ms"), now)

                gone = False
                prune()
                # force: e neste evento que mora a informacao insubstituivel.
                save_state(force=True)
                count = len(_sessions)

        self._send(200, {"ok": True, "removed": gone, "sessions": count})


PID_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "api.pid")


def write_pid():
    """Deixa o PID em disco para o tray achar este processo.

    Sem isto, parar ou reiniciar a API de fora exigiria varrer processos e
    casar linha de comando — caro no Windows e fragil. O arquivo é a resposta
    direta, e um PID obsoleto é inofensivo: quem lê confere se o processo existe
    (pid_alive) antes de fazer qualquer coisa.
    """
    try:
        with open(PID_FILE, "w", encoding="utf-8") as fh:
            fh.write(str(os.getpid()))
    except OSError:
        pass


def main():
    write_pid()
    restored = load_state()
    weather.start()
    binario = herdr.start(ao_consultar=lambda online: motor.avaliar(online, _now()))
    if binario:
        print(f"herdr: sensor ligado ({binario})")
    else:
        # Sem o sensor, ninguem chama `avaliar` — e o motor fica em HOOKS, que
        # ja e o padrao de boot. E o comportamento certo, e nao um esquecimento.
        print("herdr: nao encontrado; motor de hooks assume")
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    server.daemon_threads = True
    print(f"claude-metrics-api ouvindo em http://{HOST}:{PORT}  (TTL={TTL_SECONDS}s)")
    if restored:
        print(f"{restored} sessao(oes) restaurada(s) de {STATE_FILE}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
