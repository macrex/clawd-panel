#!/usr/bin/env python3
"""Uma pergunta só sobre o transcript do Claude Code: este turno foi
interrompido e nunca retomado?

POR QUE ISTO EXISTE
Quando você aperta Esc, o Claude Code **não dispara o hook `Stop`** — o turno não
terminou, foi abortado. O último evento que a API recebeu foi o
`UserPromptSubmit`, e ela honra esse contrato indefinidamente. Observado ao vivo:
uma sessão ficou `state=working` por 644 s com o transcript parado há 675 s,
enquanto o heartbeat chegava normalmente a cada 6 s. Do lado de fora, o painel
dizia que o Claude estava trabalhando.

POR QUE É UM ARQUIVO SEPARADO
Este é o único ponto do sistema que conhece o formato interno do transcript —
formato de terceiro, que muda sem aviso. Isolado, o dia em que mudar tem um só
lugar para consertar e um só arquivo de teste para atualizar.

A REGRA
Não é "achou a marca de interrupção". A marca aparece em TODA interrupção e uma
sessão saudável acumula várias — medido, um transcript de 5.912 linhas tinha
quatro, a última na linha 5.904, com resposta depois. O veredito é a AUSÊNCIA de
uma linha `assistant` depois da ÚLTIMA marca.

POR QUE BUSCA DE TEXTO E NÃO JSON
Isto roda no caminho de escrita da API, a cada heartbeat de cada sessão que está
trabalhando. Parsear 64 KB de JSONL a cada 10 s por sessão custaria muito mais do
que procurar duas cadeias de bytes, e a decisão não precisa de nenhum campo
estruturado — só de QUEM veio depois de QUEM.
"""

import json
import os
from datetime import datetime

# O texto que o Claude Code escreve no lugar da resposta abortada.
#
# O `"text":"` na frente NÃO é decoração: a marca real é o bloco de texto INTEIRO,
# e procurar a frase solta acha também quem apenas FALA dela. Medido num
# transcript de 6 mil linhas em que a interrupção era o assunto da conversa: a
# busca solta acha 17 linhas, das quais 3 são interrupções de verdade. E a última
# solta estava 4.600 linhas depois da última real — ou seja, a busca solta não
# erra pouco, erra tudo.
#
# Conteúdo passado a uma ferramenta (um arquivo de teste que contenha a marca,
# por exemplo) chega ao transcript com as aspas escapadas — `\"text\":\"` — e por
# isso também não casa aqui. Medido: mais 4 linhas descartadas por essa via.
MARCA = '"text":"[Request interrupted by user'

# Como uma linha de resposta se identifica no JSONL. É serialização compacta, sem
# espaço depois dos dois-pontos; se isso mudar, este é o arquivo a consertar.
RESPOSTA = '"type":"assistant"'

# 64 KB cobre dezenas de linhas — muito mais do que o necessário para saber o que
# veio depois da última marca, e barato o bastante para ler a cada heartbeat.
CAUDA = 64 * 1024


def interrompido(caminho, cauda=CAUDA):
    """O último turno foi interrompido e nunca retomado?

    Na dúvida devolve False. O hook é a fonte da verdade e esta função só tem
    permissão para contradizê-lo com prova na mão: sem arquivo legível não há
    prova, e um falso "interrompido" apagaria da tela uma sessão que está de fato
    trabalhando — o erro caro.
    """
    if not caminho:
        return False
    try:
        with open(caminho, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            tamanho = fh.tell()
            fh.seek(max(0, tamanho - cauda))
            bloco = fh.read()
    except (OSError, ValueError):
        return False

    # A cauda quase sempre começa no meio de uma linha. Descartá-la evita julgar
    # por um fragmento; quando o arquivo inteiro coube, não há o que descartar.
    if tamanho > cauda:
        quebra = bloco.find(b"\n")
        if quebra < 0:
            return False        # uma linha só, e cortada: nada confiável aqui
        bloco = bloco[quebra + 1:]

    texto = bloco.decode("utf-8", "replace")

    ultima = texto.rfind(MARCA)
    if ultima < 0:
        # Sem marca na cauda: ou nunca houve interrupção, ou ela ficou tão para
        # trás que há um arquivo inteiro de conversa depois dela. Os dois casos
        # querem a mesma resposta.
        return False

    return texto.find(RESPOSTA, ultima) < 0


# ---- Tokens por modelo ----
#
# POR QUE ISTO MORA AQUI, E NAO EM works.py
# Este modulo se declara "o unico ponto do sistema que conhece o formato interno
# do transcript" (ver o cabecalho). As armadilhas abaixo sao exatamente isso:
# conhecimento de formato de terceiro. Espalha-las por dois arquivos garante que
# so um seja consertado no dia em que o formato mudar.
#
# POR QUE JSON AQUI, SE `interrompido` EVITA JSON DE PROPOSITO
# O argumento la e de custo: `interrompido` roda a cada heartbeat de cada sessao
# que trabalha. Este NAO roda: `uso` e chamado uma vez por turno FECHADO. A
# ordem de grandeza e de centenas de chamadas por dia contra dezenas de milhares.

# Quanto da cauda ler para achar as respostas de UM turno. Um turno que produza
# mais do que isto de transcript nao existe na pratica — o maior arquivo desta
# maquina tem 157 MB para uma sessao INTEIRA, de centenas de turnos.
CAUDA_USO = 8 * 1024 * 1024

# As cinco categorias de token. Mesma tupla de precos.CAMPOS, repetida aqui de
# proposito: este modulo nao importa precos, e nao deve — ele mede, nao precifica.
CAMPOS = ("input", "output", "cache_5m", "cache_1h", "cache_read")

# Modelo interno do Claude Code, sem `usage`. Nao tem preco e nao deve receber
# um por aproximacao.
SINTETICO = "<synthetic>"


def _epoch(s):
    """Carimbo ISO do transcript -> epoch. None quando nao da para ler."""
    try:
        return datetime.fromisoformat(str(s).replace("Z", "+00:00")).timestamp()
    except (ValueError, AttributeError, TypeError):
        return None


def subagentes(caminho):
    """Os transcripts de subagente deste transcript. Lista vazia quando nao ha.

    O Claude Code guarda o trabalho delegado em `<sessao>/subagents/*.jsonl`,
    ao lado de `<sessao>.jsonl`. Sao arquivos separados, sem linha humana
    nenhuma — o subagente nao conversa com voce, conversa com o agente.

    Medido no acervo desta maquina: 166 arquivos, 142 MB, e **todo** o Sonnet 5
    esta neles. Quem ler so o arquivo da sessao subconta o turno que delegou.
    """
    if not caminho:
        return []
    base, _ = os.path.splitext(caminho)
    pasta = os.path.join(base, "subagents")
    try:
        return sorted(os.path.join(pasta, n) for n in os.listdir(pasta)
                      if n.endswith(".jsonl"))
    except OSError:
        return []       # sessao que nunca delegou: o diretorio nem existe


def uso(caminho, inicio, fim, cauda=CAUDA_USO, com_subagentes=True):
    """Tokens por modelo nas respostas entre `inicio` e `fim` (epoch).

    Devolve {id_do_modelo: {input, output, cache_5m, cache_1h, cache_read}}.

    Inclui os subagentes que rodaram nessa janela, e a soma e proposital: um
    turno custou o que custou, inclusive o que ele delegou. Separar responderia
    "quanto o agente principal gastou", que nao e a pergunta que alguem faz
    olhando o livro-caixa.

    Vazio em qualquer duvida — mesma disciplina de `interrompido()`: sem prova,
    nao se afirma nada. Um total inventado poluiria o livro-caixa para sempre.
    """
    fora = {}
    if not caminho:
        return fora
    # UMA cobranca por mensagem, mesmo que ela ocupe varias linhas. Ver
    # `_acumular`. O conjunto atravessa os arquivos porque a sessao e os
    # subagentes dela nunca compartilham id — e se compartilhassem, seria a
    # mesma cobranca mesmo.
    vistos = set()
    _acumular(caminho, inicio, fim, cauda, fora, vistos)
    if com_subagentes:
        for filho in subagentes(caminho):
            _acumular(filho, inicio, fim, cauda, fora, vistos)
    return fora


def _acumular(caminho, inicio, fim, cauda, fora, vistos):
    """Soma um transcript dentro de `fora`. Silencioso em qualquer falha."""
    try:
        with open(caminho, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            tamanho = fh.tell()
            fh.seek(max(0, tamanho - cauda))
            bloco = fh.read()
    except (OSError, ValueError):
        return

    # A cauda quase sempre comeca no meio de uma linha; julgar por um fragmento
    # daria numero errado em vez de numero nenhum.
    if tamanho > cauda:
        quebra = bloco.find(b"\n")
        if quebra < 0:
            return
        bloco = bloco[quebra + 1:]

    for linha in bloco.decode("utf-8", "replace").splitlines():
        # Filtro barato antes do parse: a maioria das linhas do transcript e
        # resultado de ferramenta e nao interessa aqui.
        #
        # Procura so a PALAVRA, e nao o par `"type":"assistant"` que
        # `interrompido` usa. O par depende de o Claude Code serializar sem
        # espaco depois dos dois-pontos — verdade hoje, mas uma decisao de
        # terceiro sobre formatacao. Aqui isso seria caro demais: um espaco a
        # mais faria `uso` devolver vazio para todo turno, e vazio e um valor
        # legitimo (transcript ilegivel), entao ninguem perceberia. O tipo de
        # verdade e confirmado no `d.get("type")` logo abaixo, depois do parse.
        if '"assistant"' not in linha:
            continue
        try:
            d = json.loads(linha)
        except ValueError:
            continue            # linha truncada nao derruba o arquivo inteiro
        if d.get("type") != "assistant":
            continue
        t = _epoch(d.get("timestamp"))
        if t is None or t < inicio or t > fim:
            continue
        m = d.get("message")
        if not isinstance(m, dict):
            continue
        mid = m.get("model")
        u = m.get("usage")
        if not mid or mid == SINTETICO or not isinstance(u, dict):
            continue

        # ARMADILHA, e a mais cara das tres: o transcript grava UMA LINHA POR
        # BLOCO DE CONTEUDO, e cada uma repete o `usage` da mensagem INTEIRA.
        # Uma resposta com texto + tres ferramentas vira quatro linhas com o
        # mesmo `cache_read_input_tokens`.
        #
        # Medido nesta sessao: 346 linhas `assistant` para 161 mensagens reais.
        # Somar por linha inflava a conta em 2,1x — e o erro so apareceu porque
        # a afericao contra o `cost_usd` do proprio Claude Code deu fator 2,2
        # em vez de 1,0. Sem essa afericao, o livro-caixa inteiro estaria com o
        # dobro do valor e nada denunciaria.
        #
        # `message.id` e a chave; `requestId` cobre transcript sem ele. Sem
        # nenhum dos dois nao da para deduplicar, e ai conta-se — subcontar por
        # medo seria trocar um erro conhecido por outro.
        chave = m.get("id") or d.get("requestId")
        if chave is not None:
            if chave in vistos:
                continue
            vistos.add(chave)

        a = fora.setdefault(mid, {c: 0 for c in CAMPOS})
        a["input"] += u.get("input_tokens") or 0
        a["output"] += u.get("output_tokens") or 0
        a["cache_read"] += u.get("cache_read_input_tokens") or 0

        # ARMADILHA: `usage.iterations[]` REPETE estes mesmos totais. Somar o
        # array dobra a conta, em silencio. So o nivel de topo entra.
        cc = u.get("cache_creation")
        if isinstance(cc, dict):
            a["cache_5m"] += cc.get("ephemeral_5m_input_tokens") or 0
            a["cache_1h"] += cc.get("ephemeral_1h_input_tokens") or 0
        else:
            # Transcript anterior a quebra por TTL: tudo no campo plano. Vai
            # para 5m por ser o mais barato — subestimar e melhor do que
            # inflar um numero que ninguem pediu.
            a["cache_5m"] += u.get("cache_creation_input_tokens") or 0
