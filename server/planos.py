#!/usr/bin/env python3
"""O plano de cada conta, lido do que a propria CLI grava em disco.

POR QUE ISTO EXISTE
O painel dizia o modelo de um agente do Claude Code e nada mais. Nao dizia o
plano de conta nenhuma — nem do Claude. Estes tres campos estao no disco desde
sempre, escritos pelas proprias CLIs; ninguem tinha ido buscar.

REGRA DE OURO DESTE MODULO
Nenhuma funcao levanta. Formato novo, arquivo ausente, JSON quebrado: tudo vira
None, e a tela mostra "—". Um painel que cai porque a OpenAI mudou o nome de um
campo seria pior do que um painel que nao sabe o plano.
"""

import base64
import json

# O plano do Claude NAO esta em `subscriptionType` — esse campo nao existe no
# `~/.claude.json`. Ele sai do PAR organizationType + organizationRateLimitTier,
# e so o `claude_max` precisa do segundo para separar 5x de 20x.
#
# Tier desconhecido, ausente ou de tipo errado NAO cai em None: cai em "Max"
# (ver plano_claude). O organizationType ja confirmou que a conta e Max — so
# nao sabemos QUAL cota. Devolver None jogaria fora justamente a informacao de
# quem paga mais, o mesmo raciocinio que faz plano_codex capitalizar um plano
# desconhecido em vez de apagar o plano inteiro.
TIERS_CLAUDE_MAX = {
    "default_claude_max_5x": "Max 5x",
    "default_claude_max_20x": "Max 20x",
}

PLANOS_CODEX = {
    "free": "Free",
    "plus": "Plus",
    "pro": "Pro",
    "team": "Team",
    "business": "Business",
    "enterprise": "Enterprise",
}

# A chave do bloco de claims da OpenAI dentro do id_token. E uma URL de
# proposito (namespace de claim customizada, como o OIDC manda), e por isso nao
# da para acha-la por prefixo.
CLAIM_OPENAI = "https://api.openai.com/auth"


def plano_claude(dados):
    """"Max 5x" a partir do `~/.claude.json` ja lido. None quando nao se sabe."""
    if not isinstance(dados, dict):
        return None
    oa = dados.get("oauthAccount")
    if not isinstance(oa, dict):
        return None
    tipo = oa.get("organizationType")
    if tipo == "claude_max":
        tier = oa.get("organizationRateLimitTier")
        # A guarda de tipo vem ANTES do `.get()`: um tier vindo de JSON pode
        # ser lista ou dicionario (tipo nao-hashable), e usa-lo como chave sem
        # checar levanta TypeError — o unico ponto do modulo onde um valor
        # externo de tipo arbitrario vira chave de dict. O caminho do Codex ja
        # tinha esta guarda (isinstance antes de PLANOS_CODEX.get); aqui
        # faltava.
        if isinstance(tier, str):
            return TIERS_CLAUDE_MAX.get(tier, "Max")
        return "Max"
    if tipo == "claude_pro":
        # "Pro" fica inline e nao na tabela porque nao tem tier para separar.
        return "Pro"
    return None


def _payload_jwt(token):
    """As claims de um JWT, sem validar assinatura. None em qualquer duvida.

    Nao validamos de proposito: o token ja esta no disco do dono da conta, e
    quem o valida e a OpenAI. Aqui ele e so um arquivo de configuracao com uma
    codificacao incomum.
    """
    if not isinstance(token, str):
        return None
    partes = token.split(".")
    if len(partes) != 3:
        return None
    corpo = partes[1]
    # base64url SEM padding: recompor o `=` antes de decodificar. Sem isto o
    # decode levanta binascii.Error na maioria dos tokens reais.
    corpo += "=" * (-len(corpo) % 4)
    try:
        return json.loads(base64.urlsafe_b64decode(corpo))
    except (ValueError, TypeError):
        return None


def plano_codex(id_token):
    """"Free" a partir do `id_token` do `~/.codex/auth.json`."""
    claims = _payload_jwt(id_token)
    if not isinstance(claims, dict):
        return None
    auth = claims.get(CLAIM_OPENAI)
    if not isinstance(auth, dict):
        return None
    tipo = auth.get("chatgpt_plan_type")
    if not isinstance(tipo, str) or not tipo:
        return None
    # Capitaliza o desconhecido em vez de devolver None: um plano novo da
    # OpenAI e informacao boa, so nao esta na tabela. "—" seria pior.
    return PLANOS_CODEX.get(tipo, tipo.capitalize())


def plano_declarado(declarados, agente):
    """O plano escrito a mao no `planos.json`. None quando nao ha entrada.

    Existe para os provedores que NAO expoem o plano em disco — o Antigravity e
    o caso: varridos o state, o log e os bancos, nenhum tier aparece. So via
    `loadCodeAssist` na rede, com refresh de OAuth. Uma linha de JSON que muda
    uma vez por ano custa menos e nunca quebra sozinha.
    """
    if not isinstance(declarados, dict):
        return None
    v = declarados.get(agente)
    if not isinstance(v, str):
        return None
    # O arquivo e escrito a mao: espaco em volta do valor e erro humano
    # plausivel, e chegaria ate a tela de 320px sem este strip().
    v = v.strip()
    return v if v else None
