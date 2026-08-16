#!/usr/bin/env python3
"""Reconstroi o livro-caixa a partir dos transcripts que ja existem.

    python tools/backfill_works.py [--out works.jsonl] [--dry]

POR QUE ISTO EXISTE
O livro-caixa so grava dali para a frente, e o passado esta todo em disco: cada
sessao do Claude Code deixa um `.jsonl` completo em ~/.claude/projects. Uma
maquina em uso ha semanas tem centenas de turnos ja registrados — esperar
acumular do zero seria jogar isso fora.

O QUE E UM TRABALHO AQUI
O mesmo do livro-caixa ao vivo: da mensagem HUMANA ate a ultima resposta do
assistente antes da proxima mensagem humana. A distincao importa porque a
maioria das linhas `user` do transcript nao e voce — sao resultados de
ferramenta voltando para o modelo. Medido num transcript de 7 turnos: 53 linhas
`user` de ferramenta contra 7 suas. Quem separa e `origin.kind == "human"`.

O QUE ESTE CAMINHO SABE, E O QUE ELE NAO SABE
O transcript tem `usage` por resposta: tokens de entrada, de saida e de cache.
Nao tem custo em dolar nem linhas alteradas — esses vem da statusline, que so o
caminho ao vivo enxerga.

Entao os registros reconstruidos saem com `cost_usd`, `lines_added` e
`lines_removed` em null, e com os tokens que o caminho ao vivo NAO tem. Cada um
leva `reconstruido: true`, para nunca serem confundidos com medicao direta —
e para nao serem gravados duas vezes se este script rodar de novo.

SOBRE PRECIFICAR OS TOKENS
Uma versao anterior deste arquivo dizia aqui que estimar custo a partir de token
"seria facil e seria mentira, porque a tabela de preco nao esta aqui". A tabela
agora existe, versionada, em server/precos.py — e quem precifica e o
`works.por_modelo`, marcando `estimado` em todo turno que nao tem `cost_usd`
medido. A objecao era a ausencia da tabela, e nao o ato de precificar.

O que continua verdade, e vale repetir: o custo de um dia antigo sai a preco de
HOJE. Se um preco mudar, o passado muda junto.
"""

import argparse
import json
import os
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "server"))
import transcript                                         # noqa: E402
import works                                              # noqa: E402

PROJETOS = Path.home() / ".claude" / "projects"
MARCA = "[Request interrupted by user"


def instante(s):
    """Carimbo ISO do transcript -> epoch."""
    try:
        return datetime.fromisoformat(str(s).replace("Z", "+00:00")).timestamp()
    except (ValueError, AttributeError):
        return None


def humano(d):
    o = d.get("origin")
    return isinstance(o, dict) and o.get("kind") == "human"


def texto(d):
    c = (d.get("message") or {}).get("content")
    if isinstance(c, str):
        return c
    if isinstance(c, list):
        return " ".join(b.get("text", "") for b in c
                        if isinstance(b, dict) and b.get("type") == "text")
    return ""


def ferramentas(d):
    c = (d.get("message") or {}).get("content")
    if not isinstance(c, list):
        return []
    return [b.get("name") for b in c
            if isinstance(b, dict) and b.get("type") == "tool_use" and b.get("name")]


def linhas_uteis(caminho):
    """So `user` e `assistant` com carimbo, na ordem do arquivo."""
    try:
        f = open(caminho, encoding="utf-8", errors="replace")
    except OSError:
        return []
    fora = []
    with f:
        for linha in f:
            try:
                d = json.loads(linha)
            except ValueError:
                continue          # linha truncada nao derruba o arquivo inteiro
            if d.get("type") in ("user", "assistant") and instante(d.get("timestamp")):
                fora.append(d)
    return fora


def chave_mensagem(d):
    """Identidade da COBRANCA desta linha. None quando nao da para saber.

    ARMADILHA: o transcript grava uma linha por BLOCO DE CONTEUDO, e todas
    repetem o `usage` da mensagem inteira. Medido numa sessao real: 346 linhas
    `assistant` para 161 mensagens — somar por linha inflava a conta em 2,1x.

    Este defeito e anterior a este trabalho: a primeira versao do backfill ja
    somava por linha. So apareceu quando o total tabelado foi conferido contra
    o `cost_usd` do proprio Claude Code e deu fator 2,2 em vez de 1,0.
    """
    return (d.get("message") or {}).get("id") or d.get("requestId")


def somar_usage(destino, mid, u):
    """Soma um bloco `usage` no acumulador por modelo.

    ARMADILHA: `usage.iterations[]` REPETE os totais do topo. Somar o array
    dobra a conta, em silencio. So o nivel de topo entra.
    """
    a = destino.setdefault(mid, {c: 0 for c in transcript.CAMPOS})
    a["input"] += u.get("input_tokens") or 0
    a["output"] += u.get("output_tokens") or 0
    a["cache_read"] += u.get("cache_read_input_tokens") or 0
    cc = u.get("cache_creation")
    if isinstance(cc, dict):
        a["cache_5m"] += cc.get("ephemeral_5m_input_tokens") or 0
        a["cache_1h"] += cc.get("ephemeral_1h_input_tokens") or 0
    else:
        a["cache_5m"] += u.get("cache_creation_input_tokens") or 0


def somar_subagentes(caminho, turnos):
    """Atribui o trabalho delegado ao turno que o gerou.

    O `rglob` sozinho nao resolve isto, e foi a descoberta que custou caro: um
    transcript de subagente nao tem UMA linha `origin.kind == "human"` — medido,
    13 `user` e 21 `assistant`, nenhuma humana. Entao `reconstruir` nunca abre
    um turno neles, e os arquivos ficam alcancaveis mas mudos.

    O subagente roda DENTRO de um turno, entao o dono e o ultimo turno que
    comecou antes dele. Nao se exige tambem `t <= ended`: o subagente pode
    devolver depois da ultima fala do agente principal, e nesse caso o trabalho
    continua sendo daquele turno — descartar seria perder justamente o turno
    que delegou mais.
    """
    if not turnos:
        return
    for filho in transcript.subagentes(caminho):
        for d in linhas_uteis(filho):
            if d.get("type") != "assistant":
                continue
            t = instante(d["timestamp"])
            m = d.get("message") or {}
            mid = m.get("model")
            u = m.get("usage")
            if (t is None or not mid or mid == transcript.SINTETICO
                    or not isinstance(u, dict)):
                continue
            k = chave_mensagem(d)
            dono = None
            for w in turnos:
                if w["started"] <= t:
                    dono = w
                else:
                    break       # `turnos` esta em ordem cronologica
            if dono is None:
                continue        # subagente anterior ao primeiro turno humano
            if k is not None:
                if k in dono["vistos"]:
                    continue
                dono["vistos"].add(k)
            somar_usage(dono["por_modelo"], mid, u)
            dono["delegado"] = True


def reconstruir(caminho, sid):
    """Lista de registros no formato do livro-caixa."""
    turnos, atual = [], None

    def fecha():
        if atual and atual["respostas"]:
            turnos.append(atual)

    for d in linhas_uteis(caminho):
        t = instante(d["timestamp"])
        if d.get("type") == "user" and humano(d):
            fecha()
            atual = {
                "sid": sid, "started": t, "ended": t, "respostas": 0,
                "tools": [], "interrompido": False, "maior_vao": 0.0,
                # Tokens POR MODELO, e nao quatro contadores planos: um turno
                # pode misturar modelos, e somar tudo num balde so perderia
                # justamente a informacao que este trabalho existe para dar.
                "por_modelo": {},
                # Ids de cobranca ja contados neste turno. Ver chave_mensagem.
                "vistos": set(),
                "model": None, "effort": None, "repo": None, "branch": None,
            }
            continue
        if atual is None:
            continue              # transcript que comeca no meio de um turno
        if d.get("type") == "assistant":
            vao = t - atual["ended"]
            if vao > atual["maior_vao"]:
                atual["maior_vao"] = vao
            atual["ended"] = t
            atual["respostas"] += 1
            atual["tools"] += ferramentas(d)
            m = d.get("message") or {}
            u = m.get("usage") or {}
            mid = m.get("model")
            k = chave_mensagem(d)
            if mid and mid != transcript.SINTETICO and (
                    k is None or k not in atual["vistos"]):
                if k is not None:
                    atual["vistos"].add(k)
                somar_usage(atual["por_modelo"], mid, u)
            atual["model"] = mid or atual["model"]
            atual["effort"] = d.get("effort") or atual["effort"]
            atual["branch"] = d.get("gitBranch") or atual["branch"]
            if d.get("cwd") and not atual["repo"]:
                atual["repo"] = os.path.basename(str(d["cwd"]).rstrip("/\\"))
        elif MARCA in texto(d):
            atual["interrompido"] = True
    fecha()

    # Depois de fechar todos os turnos, e nao durante: a atribuicao precisa da
    # lista inteira para achar o dono de cada mensagem delegada.
    somar_subagentes(caminho, turnos)
    return [registro(w) for w in turnos]


def registro(w):
    """Um trabalho reconstruido, com as MESMAS chaves do caminho ao vivo."""
    # O modelo PRINCIPAL do turno e o que mais produziu saida, e nao o ultimo a
    # responder: num turno misto o ultimo pode ser o Haiku de uma tarefa de
    # fundo, que nao descreve o turno.
    porm = w.get("por_modelo") or {}
    principal = (max(porm, key=lambda k: porm[k].get("output", 0))
                 if porm else None)

    reg = {
        "session_id": w["sid"][:8],
        "started": round(w["started"], 3),
        "ended": round(w["ended"], 3),
        "seconds": round(max(0.0, w["ended"] - w["started"]), 1),
        # Bloqueio nao da para reconstruir: o transcript nao registra o instante
        # em que um pedido de permissao apareceu na tela. Zero seria afirmar que
        # nao houve.
        "blocked_seconds": None,
        "gap_seconds": round(w["maior_vao"], 1),
        # Tempo de API tambem nao: o que existe e o relogio de parede entre as
        # respostas, que inclui o tempo de ferramenta.
        "api_seconds": None,
        "blocks": None,
        "repo": w["repo"],
        "branch": w["branch"],
        "model": w["model"],
        "model_id": principal,
        # O transcript nao registra modo rapido: ele so existe no payload da
        # statusline. False e o que se SABE, e nao um palpite — um turno
        # reconstruido nunca sera precificado em dobro por engano.
        "fast": False,
        "tokens": porm,
        "effort": w["effort"],
        "cost_usd": None,
        "lines_added": None,
        "lines_removed": None,
        # O que SO este caminho tem.
        "respostas": w["respostas"],
        "tools": len(w["tools"]),
        "reconstruido": True,
    }
    if w["interrompido"]:
        reg["interrupted"] = True
    # Este turno delegou a subagente. Marcado porque muda como o numero se le:
    # um turno que delegou tem custo de varios modelos sem que voce tenha
    # trocado de modelo nenhuma vez.
    if w.get("delegado"):
        reg["delegado"] = True
    return reg


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", default=works.CAMINHO)
    p.add_argument("--dry", action="store_true", help="so mostra, nao grava")
    args = p.parse_args()

    if not PROJETOS.is_dir():
        print(f"erro: {PROJETOS} nao existe")
        return 1

    # Ja gravados: um `started` do mesmo id nunca entra duas vezes. Assim o
    # script pode rodar de novo depois de mais uma semana de conversas.
    vistos = {(r.get("session_id"), r.get("started"))
              for r in works.ler(args.out)}

    total = novos = 0
    # rglob, e nao glob("*/*.jsonl"): os transcripts de SUBAGENTE moram um
    # nivel mais fundo, em <proj>/<sessao>/subagents/. Sao 166 arquivos e 36%
    # do acervo nesta maquina, e o glob antigo nao os via — o Sonnet 5 inteiro
    # ficava invisivel porque so roda em subagente.
    for arq in sorted(PROJETOS.rglob("*.jsonl")):
        regs = reconstruir(arq, arq.stem)
        if not regs:
            continue
        gravados = 0
        for r in regs:
            total += 1
            if (r["session_id"], r["started"]) in vistos:
                continue
            if args.dry or works.gravar(r, args.out):
                gravados += 1
                novos += 1
        print(f"{arq.parent.name[:34]:36}{arq.stem[:8]}  {len(regs):>4} turnos, "
              f"{gravados:>4} novos")

    print(f"\n{total} turnos encontrados, {novos} gravados"
          f"{' (simulacao)' if args.dry else ''} em {args.out}")

    resumo = works.resumo(works.ler(args.out), now=0)
    print("\nlivro-caixa agora:")
    for k in ("trabalhos", "seconds", "mediana_seconds", "gap_seconds",
              "marcados"):
        if k in resumo:
            print(f"  {k:<18}{resumo[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
