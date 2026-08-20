#!/usr/bin/env python3
"""Testes de `planos.py`.

Este arquivo prova a fronteira com tres formatos de terceiros: o
`~/.claude.json` da Anthropic, o `id_token` (JWT) do `~/.codex/auth.json` da
OpenAI, e o `planos.json` escrito a mao para provedores sem plano legivel em
disco. Cada campo que chega de fora e tratado como entrada hostil — tipo
errado, chave ausente, ou formato futuro que ainda nao existe. Um teste que
falhar aqui e sempre sobre o QUE MUDOU no disco de outra empresa, nunca sobre
logica interna.
"""

import base64
import json
import unittest

import planos


class TestClaude(unittest.TestCase):
    # O `~/.claude.json` NAO tem `subscriptionType`: o plano so se le pelo PAR
    # organizationType + organizationRateLimitTier. Foi o primeiro caminho
    # falso do levantamento, e este teste existe para ninguem procurar de
    # novo.
    def test_max_5x(self):
        d = {"oauthAccount": {"organizationType": "claude_max",
                              "organizationRateLimitTier": "default_claude_max_5x"}}
        self.assertEqual(planos.plano_claude(d), "Max 5x")

    def test_max_20x(self):
        d = {"oauthAccount": {"organizationType": "claude_max",
                              "organizationRateLimitTier": "default_claude_max_20x"}}
        self.assertEqual(planos.plano_claude(d), "Max 20x")

    # Tier que a Anthropic ainda nao lancou (ou que ja descontinuou) NAO pode
    # apagar o plano: a conta ja e sabidamente Max, so nao sabemos a cota.
    def test_max_tier_desconhecido_vira_max(self):
        d = {"oauthAccount": {"organizationType": "claude_max",
                              "organizationRateLimitTier": "default_claude_max_50x"}}
        self.assertEqual(planos.plano_claude(d), "Max")

    def test_max_tier_ausente_vira_max(self):
        d = {"oauthAccount": {"organizationType": "claude_max"}}
        self.assertEqual(planos.plano_claude(d), "Max")

    # Regressao do bug: tier vindo de JSON pode ser lista ou dicionario (tipo
    # nao-hashable). Usa-lo direto como chave de TIERS_CLAUDE_MAX levantava
    # TypeError, contra a regra de ouro do modulo.
    def test_max_tier_lista_nao_levanta(self):
        d = {"oauthAccount": {"organizationType": "claude_max",
                              "organizationRateLimitTier": ["default_claude_max_5x"]}}
        self.assertEqual(planos.plano_claude(d), "Max")

    def test_max_tier_dict_nao_levanta(self):
        d = {"oauthAccount": {"organizationType": "claude_max",
                              "organizationRateLimitTier": {"x": 1}}}
        self.assertEqual(planos.plano_claude(d), "Max")

    def test_pro_ignora_o_tier(self):
        d = {"oauthAccount": {"organizationType": "claude_pro",
                              "organizationRateLimitTier": "qualquer_coisa"}}
        self.assertEqual(planos.plano_claude(d), "Pro")

    def test_tipo_desconhecido_e_none(self):
        d = {"oauthAccount": {"organizationType": "claude_futuro",
                              "organizationRateLimitTier": "default_x"}}
        self.assertIsNone(planos.plano_claude(d))

    def test_sem_oauth_account_e_none(self):
        self.assertIsNone(planos.plano_claude({}))

    # `oauthAccount` presente mas do tipo errado — nao so ausente. Ninguem
    # exercitava este ramo do isinstance antes.
    def test_oauth_account_invalido_e_none(self):
        self.assertIsNone(planos.plano_claude({"oauthAccount": "nao-e-dict"}))
        self.assertIsNone(planos.plano_claude({"oauthAccount": None}))

    def test_entrada_que_nao_e_dicionario_e_none(self):
        self.assertIsNone(planos.plano_claude(None))
        self.assertIsNone(planos.plano_claude([1, 2, 3]))


class TestCodex(unittest.TestCase):
    def _jwt(self, claims):
        """Um JWT de mentirinha. A assinatura nao importa: nos so LEMOS o
        payload, nunca validamos — quem valida e a OpenAI, e o token ja esta
        no disco do dono da conta."""
        cab = base64.urlsafe_b64encode(b'{"alg":"none"}').rstrip(b"=").decode()
        corpo = base64.urlsafe_b64encode(
            json.dumps(claims).encode()).rstrip(b"=").decode()
        return f"{cab}.{corpo}.assinatura-falsa"

    # A URL abaixo e escrita A MAO de proposito, e nao importada de
    # `planos.CLAIM_OPENAI` — um teste que le a propria constante do modulo
    # sob teste nao prova nada (mesma regra de `server/test_precos.py`). Sem
    # esta nota, o primeiro refactor "DRY" esvazia estes testes.
    def test_free(self):
        t = self._jwt({"https://api.openai.com/auth": {"chatgpt_plan_type": "free"}})
        self.assertEqual(planos.plano_codex(t), "Free")

    def test_plus(self):
        t = self._jwt({"https://api.openai.com/auth": {"chatgpt_plan_type": "plus"}})
        self.assertEqual(planos.plano_codex(t), "Plus")

    def test_plano_desconhecido_capitaliza(self):
        t = self._jwt({"https://api.openai.com/auth": {"chatgpt_plan_type": "galactico"}})
        self.assertEqual(planos.plano_codex(t), "Galactico")

    def test_token_sem_o_bloco_de_auth_e_none(self):
        self.assertIsNone(planos.plano_codex(self._jwt({"email": "x@y.z"})))

    def test_token_quebrado_nao_levanta(self):
        self.assertIsNone(planos.plano_codex("isto-nao-e-um-jwt"))
        self.assertIsNone(planos.plano_codex("a.b"))
        self.assertIsNone(planos.plano_codex(""))
        self.assertIsNone(planos.plano_codex(None))
        # Tres partes, corpo lixo: e o unico jeito de exercitar o
        # `except (ValueError, TypeError)` de `_payload_jwt` — todos os casos
        # acima falham ANTES, na checagem de numero de partes.
        self.assertIsNone(planos.plano_codex("a.b.c"))              # base64 invalido
        self.assertIsNone(planos.plano_codex("a.héllo.c"))          # corpo nao-ASCII
        self.assertIsNone(planos.plano_codex("a.b.c.d"))            # partes demais

    # O payload do JWT usa base64url SEM padding. Decodificar sem recompor o
    # `=` levanta binascii.Error, e era o jeito obvio de errar isto.
    def test_payload_sem_padding(self):
        claims = {"https://api.openai.com/auth": {"chatgpt_plan_type": "pro"}}
        t = self._jwt(claims)
        corpo = t.split(".")[1]
        self.assertNotEqual(len(corpo) % 4, 0,
                             "o teste so vale se o payload precisar de padding")
        self.assertEqual(planos.plano_codex(t), "Pro")

    # JSON valido mas que nao e objeto (array, numero...) tambem precisa cair
    # em None — nada garante que o payload decodificado seja um dict antes do
    # isinstance checar.
    def test_payload_json_valido_mas_nao_e_objeto_e_none(self):
        self.assertIsNone(planos.plano_codex(self._jwt([1, 2, 3])))
        self.assertIsNone(planos.plano_codex(self._jwt(42)))

    def test_bloco_auth_nao_e_dict_e_none(self):
        t = self._jwt({"https://api.openai.com/auth": "nao-e-dict"})
        self.assertIsNone(planos.plano_codex(t))

    def test_chatgpt_plan_type_tipo_errado_e_none(self):
        self.assertIsNone(planos.plano_codex(
            self._jwt({"https://api.openai.com/auth": {"chatgpt_plan_type": 5}})))
        self.assertIsNone(planos.plano_codex(
            self._jwt({"https://api.openai.com/auth": {"chatgpt_plan_type": None}})))
        self.assertIsNone(planos.plano_codex(
            self._jwt({"https://api.openai.com/auth": {"chatgpt_plan_type": ""}})))


class TestDeclarado(unittest.TestCase):
    def test_devolve_o_rotulo(self):
        self.assertEqual(
            planos.plano_declarado({"antigravity": "AI Pro"}, "antigravity"), "AI Pro")

    def test_sem_entrada_e_none(self):
        self.assertIsNone(planos.plano_declarado({"antigravity": "AI Pro"}, "cursor"))

    def test_com_arquivo_ausente_e_none(self):
        self.assertIsNone(planos.plano_declarado({}, "antigravity"))
        self.assertIsNone(planos.plano_declarado(None, "antigravity"))

    # O `planos.json` e escrito a mao. `null`, string vazia e numero sao o
    # erro humano esperado, nao um caso exotico.
    def test_valores_invalidos_e_none(self):
        self.assertIsNone(planos.plano_declarado({"antigravity": None}, "antigravity"))
        self.assertIsNone(planos.plano_declarado({"antigravity": ""}, "antigravity"))
        self.assertIsNone(planos.plano_declarado({"antigravity": 5}, "antigravity"))

    def test_espacos_sao_removidos(self):
        self.assertEqual(
            planos.plano_declarado({"antigravity": "  AI Pro  "}, "antigravity"),
            "AI Pro")
        self.assertIsNone(planos.plano_declarado({"antigravity": "   "}, "antigravity"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
