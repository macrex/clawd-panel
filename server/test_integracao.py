#!/usr/bin/env python3
"""Testes de integração da fusão dentro de `/status`. Só stdlib.

`test_fusao.py` testa `fundir()` isolada e `test_herdr.py` testa
`parse_agentes()` isolada — nenhuma das duas suítes chama `build_agents()` ou
`build_status()` com o herdr ONLINE. Esta suíte fecha esse buraco: povoa
`herdr._retrato` à mão (mesma técnica de `test_herdr.py`) e chama
`build_status()` de verdade, para exercitar o laço dos órfãos, os campos
`done`/`agent`, e os dois campos novos do topo (`herdr_online`, `done`).
"""

import os
import tempfile
import time
import unittest
from unittest import mock

import claude_metrics_api as api
import herdr
import motor

# as duas linhas de transcript sinteticas, iguais as de test_estado.py
MARCA_INT = ('{"type":"user","message":{"role":"user","content":'
             '[{"type":"text","text":"[Request interrupted by user]"}]}}')
ASSIST_INT = '{"type":"assistant","message":{"role":"assistant","content":[]}}'


def sessao(sid, pane_id=None, estado=api.WORKING, repo="proj", ts=None):
    """Uma sessão nossa, como o /ingest + /event a teriam deixado."""
    agora = ts if ts is not None else time.time()
    rec = {
        "data": {"session_id": sid, "repo": repo, "model": "Opus 5 (1M context)",
                 "context_pct": 30},
        "ts": agora, "state": estado, "state_ts": agora, "limits_ts": agora,
    }
    if pane_id:
        rec["herdr_pane_id"] = pane_id
    return sid, rec


def dele(pane, estado, agente="claude", repo="x", seq=None):
    """Um agente como o herdr o devolve, já passado por `parse_agentes`."""
    return {"pane_id": pane, "state": estado, "agent": agente,
            "cwd": "/tmp/" + repo, "repo": repo, "focused": False, "name": "",
            "seq": seq}


class TesteIntegracaoStatus(unittest.TestCase):

    def setUp(self):
        api._sessions.clear()
        # build_status() agora popula _divergencias (Tarefa 6): sem limpar
        # aqui, um teste que fizer os dois lados discordarem vazava a entrada
        # para o proximo teste desta classe, que roda no mesmo processo.
        api._divergencias.clear()
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}

    def tearDown(self):
        api._sessions.clear()
        api._divergencias.clear()
        # Restaura o retrato do herdr para não vazar estado entre suítes: elas
        # rodam no mesmo processo Python quando chamadas em sequência.
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}

    def povoar(self, *pares):
        for sid, rec in pares:
            api._sessions[sid] = rec

    def test_orfao_do_herdr_aparece_com_as_mesmas_chaves_de_um_agente_normal(self):
        self.povoar(sessao("s1", pane_id=None, estado=api.WORKING, repo="nosso"))
        herdr._aplicar([dele("w9:p9", "working", agente="codex", repo="alheio")])

        s = api.build_status()
        self.assertTrue(s["herdr_online"])
        self.assertEqual(len(s["labels"]), 2)

        normal = next(a for a in s["labels"] if a["agent"] == "claude")
        orfao = next(a for a in s["labels"] if a["agent"] == "codex")
        self.assertEqual(orfao["repo"], "alheio")
        # O painel desenha "-" nos campos que faltam (src/ui.cpp:362): para
        # isso, o órfão precisa ter exatamente as mesmas chaves que um agente
        # normal, mesmo que várias venham None.
        self.assertEqual(set(normal.keys()), set(orfao.keys()))

    def test_orfao_done_vira_idle_no_state_e_sinaliza_done(self):
        self.povoar(sessao("s1", pane_id=None, repo="nosso"))
        herdr._aplicar([dele("w2:p3", "done", agente="codex", repo="alheio")])

        s = api.build_status()
        orfao = next(a for a in s["labels"] if a["agent"] == "codex")
        # A placa mapeia string desconhecida para Unknown (status.cpp:20): um
        # `state` de "done" apareceria como "estado desconhecido". A string
        # "done" NUNCA pode ir parar no campo `state`.
        self.assertEqual(orfao["state"], api.IDLE)
        self.assertNotEqual(orfao["state"], "done")
        self.assertTrue(orfao["done"])

    def test_orfao_ganha_state_age_depois_de_uma_transicao(self):
        # O tempo de turno dos agentes que nao sao nossos: o herdr nao tem
        # relogio, entao o carimbo vem da transicao que o sensor observou.
        self.povoar(sessao("s1", pane_id=None, repo="nosso"))
        herdr._aplicar([dele("w9:p9", "idle", agente="codex", seq=10)])
        herdr._aplicar([dele("w9:p9", "working", agente="codex", seq=11)])

        orfao = next(a for a in api.build_status()["labels"]
                     if a["agent"] == "codex")
        self.assertIsInstance(orfao["state_age"], int)
        self.assertLess(orfao["state_age"], 5)      # acabou de virar

    def test_orfao_sem_transicao_observada_nao_afirma_idade(self):
        # Visto pela primeira vez ja trabalhando: nao ha como saber desde
        # quando, e um numero pequeno seria mentira com cara de verdade.
        self.povoar(sessao("s1", pane_id=None, repo="nosso"))
        herdr._aplicar([dele("w9:p9", "working", agente="codex", seq=10)])

        orfao = next(a for a in api.build_status()["labels"]
                     if a["agent"] == "codex")
        self.assertIsNone(orfao["state_age"])

    def test_contagem_done_do_topo_bate_com_os_itens_sinalizados(self):
        self.povoar(
            sessao("s1", pane_id=None, repo="a"),
            sessao("s2", pane_id=None, repo="b"),
        )
        herdr._aplicar([
            dele("w1:p1", "done", agente="codex", repo="c"),
            dele("w2:p2", "working", agente="gemini", repo="d"),
            dele("w3:p3", "done", agente="cursor", repo="e"),
        ])

        s = api.build_status()
        marcados = [a for a in s["labels"] if a.get("done")]
        self.assertEqual(s["done"], len(marcados))
        self.assertEqual(s["done"], 2)

    def test_herdr_offline_sai_identico_a_antes_da_tarefa(self):
        # _retrato já está zerado pelo setUp — nenhuma chamada a _aplicar().
        self.povoar(sessao("s1", pane_id="w0:p1", estado=api.WORKING, repo="nosso"))

        s = api.build_status()
        self.assertFalse(s["herdr_online"])
        self.assertEqual(s["done"], 0)
        self.assertEqual(len(s["labels"]), 1)     # nenhum órfão: o herdr está mudo
        self.assertEqual(s["labels"][0]["state"], api.WORKING)   # a nossa máquina de estados, intocada
        self.assertFalse(s["labels"][0]["done"])
        self.assertEqual(s["labels"][0]["agent"], "claude")

    def test_agente_pareado_por_pane_id_o_estado_do_herdr_prevalece(self):
        # O caso central desta tarefa: a nossa sessão diz `working`, mas o
        # herdr já sabe que ela travou.
        self.povoar(sessao("s1", pane_id="w0:p1", estado=api.WORKING, repo="nosso"))
        herdr._aplicar([dele("w0:p1", "blocked", agente="claude", repo="nosso")])

        s = api.build_status()
        self.assertEqual(len(s["labels"]), 1)     # pareado: não vira órfão também
        self.assertEqual(s["labels"][0]["state"], api.BLOCKED)
        self.assertEqual(s["labels"][0]["session_id"], "s1"[:8])

    def test_fechar_sessao_com_divergencia_aberta_nao_deixa_entrada_orfa(self):
        # SessionEnd no meio de um episodio de divergencia (herdr e hooks
        # discordando ha menos de DIVERGENCIA_S). Sem a limpeza espelhada em
        # _handle_event, esta entrada ficava presa em _divergencias para
        # sempre: nada mais a remove, e a API roda como servico persistente,
        # reiniciado so em deploy manual.
        api._divergencias["s1"] = (time.time(), False)
        handler = api.Handler.__new__(api.Handler)   # sem __init__: sem socket
        handler._send = lambda code, payload: None
        with mock.patch.object(api, "save_state"):    # so testa a memoria, nao o disco
            handler._handle_event({"session_id": "s1", "state": "closed"})
        self.assertNotIn("s1", api._divergencias)

    def test_sem_sessoes_nossas_o_herdr_ainda_aparece_no_labels(self):
        # O bug central do item 1: zero sessoes do Claude Code, um agente do
        # herdr trabalhando. Antes da correcao, `build_status()` saia cedo
        # (branch `if not live:`) com `labels: []` e `herdr_online: True` ao
        # mesmo tempo, e o firmware desenhava "CLAUDIO OFFLINE" justamente no
        # caso para o qual este sensor foi escrito.
        herdr._aplicar([dele("w2:p3", "working", agente="codex", repo="alheio")])

        s = api.build_status()
        self.assertFalse(s["online"])          # ninguem do Claude Code publicando
        self.assertTrue(s["herdr_online"])
        self.assertEqual(len(s["labels"]), 1)
        self.assertEqual(s["labels"][0]["agent"], "codex")
        self.assertEqual(s["labels"][0]["repo"], "alheio")
        self.assertEqual(s["blocked"], 0)
        self.assertEqual(s["done"], 0)

    def test_sem_sessoes_nossas_com_herdr_bloqueado_conta_no_topo(self):
        herdr._aplicar([dele("w2:p3", "blocked", agente="codex", repo="alheio")])

        s = api.build_status()
        self.assertFalse(s["online"])
        self.assertEqual(s["blocked"], 1)
        self.assertEqual(s["labels"][0]["state"], api.BLOCKED)

    def test_sem_sessoes_nossas_e_sem_herdr_labels_continua_vazio(self):
        # herdr offline e nenhuma sessao: comportamento de sempre, sem
        # regressao.
        s = api.build_status()
        self.assertFalse(s["online"])
        self.assertFalse(s["herdr_online"])
        self.assertEqual(s["labels"], [])
        self.assertEqual(s["blocked"], 0)
        self.assertEqual(s["done"], 0)

    def test_prune_remove_divergencia_de_sessao_morta(self):
        # Mesma fuga, pela outra porta: uma sessao sem hooks que estoura o TTL
        # e podada por prune(), e nao por SessionEnd. As duas saidas de
        # _sessions precisam levar _divergencias junto.
        agora = time.time()
        velho = agora - api.TTL_SECONDS - 1
        api._sessions["s1"] = {
            "data": {}, "ts": velho, "state": api.WORKING, "state_ts": velho,
        }
        api._divergencias["s1"] = (agora, False)
        with mock.patch.object(api, "save_state"):
            api.prune()
        self.assertNotIn("s1", api._sessions)
        self.assertNotIn("s1", api._divergencias)


class TestePlanos(unittest.TestCase):
    """O bloco `planos` do /status e o modelo dos orfaos.

    As duas coisas na mesma suite porque sao a mesma leva e o mesmo risco: as
    duas leem DISCO dentro de um endpoint que a placa pede a cada dois segundos,
    e as duas so podem fazer isso porque tem cache.
    """

    def setUp(self):
        api._sessions.clear()
        api._divergencias.clear()
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}
        self.limpar_caches()

    def tearDown(self):
        api._sessions.clear()
        api._divergencias.clear()
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}
        self.limpar_caches()

    def limpar_caches(self):
        """Os caches sao de MODULO e sobrevivem entre testes: sem isto, o
        primeiro teste que rodar aquece o plano e os seguintes medem o cache
        dele em vez do proprio comportamento."""
        api._planos_cache["ts"] = 0.0
        api._planos_cache["valor"] = {}
        with api._cache_lock:
            api._modelos_cache.clear()

    def test_status_traz_o_bloco_de_planos(self):
        """`planos` e global: e da CONTA, nao do agente. Repeti-lo em cada
        entrada de `labels` sugeriria que pertence a sessao."""
        s = api.build_status()
        self.assertIsInstance(s["planos"], dict)

    def test_status_traz_o_bloco_de_planos_tambem_com_sessao_viva(self):
        # `build_status` tem DOIS returns, e o de cima (sem sessao nossa) e o
        # que o teste acima exercita. Publicar o campo so num deles faria o
        # painel perder o plano no instante em que um agente aparece.
        self.povoar_uma_sessao()
        s = api.build_status()
        self.assertIsInstance(s["planos"], dict)

    def povoar_uma_sessao(self):
        sid, rec = sessao("s1", pane_id=None, estado=api.WORKING, repo="nosso")
        api._sessions[sid] = rec

    def test_planos_sobrevive_ao_corte_do_painel(self):
        """A lista do ?campos=painel e de REMOCAO, nao de permissao. Este teste
        trava isso: alguem que a converta em lista branca quebra aqui."""
        import painel
        magro = painel.enxugar({"planos": {"claude": "Max 5x"}, "labels": []})
        self.assertEqual(magro["planos"], {"claude": "Max 5x"})

    def test_build_planos_nao_rele_o_disco_na_segunda_chamada(self):
        # O plano de conta muda uma vez por ano e o /status e pedido a cada dois
        # segundos: a segunda chamada nao pode encostar em disco nenhum.
        with mock.patch.object(api, "_ler_json", return_value=None) as ler, \
                mock.patch.object(api, "_planos_json", return_value={}) as decl:
            api.build_planos()
            leituras = ler.call_count + decl.call_count
            self.assertGreater(leituras, 0)   # a PRIMEIRA le, senao nao ha o que cachear
            api.build_planos()
            self.assertEqual(ler.call_count + decl.call_count, leituras)

    def test_build_planos_rele_quando_o_ttl_vence(self):
        # O relogio e INJETADO, nao esperado: um teste que dorme uma hora para
        # provar um TTL de uma hora nao e um teste.
        with mock.patch.object(api, "_ler_json", return_value=None) as ler, \
                mock.patch.object(api, "_planos_json", return_value={}):
            with mock.patch.object(api, "_now", return_value=1000.0):
                api.build_planos()
                leituras = ler.call_count
            with mock.patch.object(api, "_now",
                                   return_value=1000.0 + api.PLANOS_TTL_S + 1):
                api.build_planos()
        self.assertGreater(ler.call_count, leituras)

    def test_planos_declarado_nao_sobrescreve_o_descoberto(self):
        # Se um dia o Claude publicar o plano num campo novo, a linha velha do
        # planos.json nao pode mentir por cima dele.
        claude = {"oauthAccount": {"organizationType": "claude_max",
                                   "organizationRateLimitTier":
                                   "default_claude_max_5x"}}
        with mock.patch.object(api, "_ler_json",
                               side_effect=lambda c: claude if ".claude" in c
                               else None), \
                mock.patch.object(api, "_planos_json",
                                  return_value={"claude": "Pro",
                                                "agy": "AI Pro"}):
            p = api.build_planos()
        self.assertEqual(p["claude"], "Max 5x")
        self.assertEqual(p["agy"], "AI Pro")

    def test_a_chave_de_planos_e_o_mesmo_nome_do_agente_no_labels(self):
        """As duas pontas falam o MESMO vocabulario, que e o do herdr.

        Quem desenha o card casa o plano com o grupo pelo nome do agente. Uma
        chave `antigravity` em `planos` ao lado de um `labels[].agent` igual a
        `agy` nao casaria, e o sintoma — grupo do Antigravity sem plano — so
        apareceria na placa, depois de gravar.
        """
        herdr._aplicar([dele("w4D:p1", "idle", agente="agy", repo="D:")])
        with mock.patch.object(api, "_planos_json",
                               return_value={"agy": "AI Pro"}), \
                mock.patch.object(api.modelos, "modelo_antigravity",
                                  return_value=None):
            s = api.build_status()
        orfao = next(a for a in s["labels"] if a["agent"] == "agy")
        self.assertIn(orfao["agent"], s["planos"])
        self.assertEqual(s["planos"][orfao["agent"]], "AI Pro")

    def test_provedor_sem_plano_nao_entra_no_dicionario(self):
        # A placa desenha "—" para a chave ausente; string vazia no fio seria a
        # mesma coisa custando bytes.
        with mock.patch.object(api, "_ler_json", return_value=None), \
                mock.patch.object(api, "_planos_json", return_value={}):
            self.assertEqual(api.build_planos(), {})

    def test_orfao_ganha_o_modelo_da_cli_dele(self):
        herdr._aplicar([dele("w9:p9", "working", agente="codex", repo="alheio")])
        with mock.patch.object(api.modelos, "modelo_codex",
                               return_value="GPT-5.6 Terra"):
            s = api.build_status()
        orfao = next(a for a in s["labels"] if a["agent"] == "codex")
        self.assertEqual(orfao["model"], "GPT-5.6 Terra")
        # A `line` leva o modelo quando ha um: sem isto ela continuaria dizendo
        # so "Codex", com o modelo certo no campo ao lado.
        self.assertIn("GPT-5.6 Terra", orfao["line"])

    def test_orfao_sem_modelo_mantem_o_nome_do_agente_na_linha(self):
        # Sem isto `agent_line` cai no default "Claude" de `short_model`, e um
        # codex sai afirmando que e o Claude.
        herdr._aplicar([dele("w9:p9", "working", agente="codex", repo="alheio")])
        with mock.patch.object(api.modelos, "modelo_codex", return_value=None):
            s = api.build_status()
        orfao = next(a for a in s["labels"] if a["agent"] == "codex")
        self.assertIsNone(orfao["model"])
        self.assertIn("Codex", orfao["line"])

    def test_agy_e_o_nome_do_antigravity_no_herdr(self):
        # Medido: `herdr agent start --kind` lista `agy`, e nunca
        # "antigravity". Um dispatch que so conhecesse "antigravity" nunca
        # dispararia — o agente existe nesta maquina e chega como `agy`.
        with mock.patch.object(api.modelos, "modelo_antigravity",
                               return_value="Claude Opus 4.6") as sonda:
            self.assertEqual(api.modelo_do_orfao("agy", "D:\\"),
                             "Claude Opus 4.6")
        sonda.assert_called_once_with("D:\\")

    def test_antigravity_nao_e_nome_de_agente_em_lugar_nenhum(self):
        """`agy` e o nome canonico, e este teste e o que impede a volta.

        A traducao `agy` -> "antigravity" foi DESCARTADA: exigiria uma tabela
        dos 21 kinds do herdr para alguem manter, e "ANTIGRAVITY" nao cabe no
        cabecalho de grupo do card (156 px uteis; 11 caracteres contra 3).
        Aceitar "antigravity" como alias e o refactor bem-intencionado que
        desfaz a decisao em silencio — se ele voltar, quebra aqui.
        """
        with mock.patch.object(api.modelos, "modelo_antigravity") as sonda:
            self.assertIsNone(api.modelo_do_orfao("antigravity", "D:\\x"))
        sonda.assert_not_called()

    def test_gemini_nao_e_roteado_para_a_sonda_do_antigravity(self):
        # `gemini` e `agy` sao DOIS kinds distintos do herdr: Gemini CLI e
        # Antigravity. A sonda do Antigravity tem reserva ("a conversa mais
        # recente"), entao rotear o gemini para ela mostraria o modelo de OUTRO
        # programa com cara de certo.
        with mock.patch.object(api.modelos, "modelo_antigravity") as sonda:
            self.assertIsNone(api.modelo_do_orfao("gemini", "D:\\x"))
        sonda.assert_not_called()

    def test_modelo_do_orfao_nao_consulta_o_disco_duas_vezes(self):
        with mock.patch.object(api.modelos, "modelo_codex",
                               return_value="GPT-5.6") as sonda:
            api.modelo_do_orfao("codex", "D:\\x")
            api.modelo_do_orfao("codex", "D:\\x")
        sonda.assert_called_once()

    def test_modelo_do_orfao_rele_quando_o_ttl_vence(self):
        """O cache PEGA e o cache SOLTA. Sem este, so a metade facil tem prova.

        O relogio e INJETADO, pelo mesmo motivo do TTL de uma hora: um teste que
        dorme trinta segundos para provar um TTL de trinta segundos nao e um
        teste. E sem ele, trocar a condicao por um `if c:` cru transformaria o
        cache em ETERNO — o modelo do orfao congelaria no primeiro valor visto,
        que e exatamente o que MODELOS_TTL_S existe para evitar — com a suite
        inteira passando.
        """
        with mock.patch.object(api.modelos, "modelo_codex",
                               side_effect=["GPT-5.6", "GPT-6"]) as sonda:
            with mock.patch.object(api, "_now", return_value=1000.0):
                self.assertEqual(api.modelo_do_orfao("codex", "D:\\x"),
                                 "GPT-5.6")
            with mock.patch.object(api, "_now",
                                   return_value=1000.0 + api.MODELOS_TTL_S + 1):
                self.assertEqual(api.modelo_do_orfao("codex", "D:\\x"), "GPT-6")
        self.assertEqual(sonda.call_count, 2)

    def test_dentro_do_ttl_o_valor_velho_continua_valendo(self):
        # O outro lado da mesma moeda: o relogio andando POUCO nao pode soltar o
        # cache, senao o TTL nao existe e a sonda roda a cada poll.
        with mock.patch.object(api.modelos, "modelo_codex",
                               side_effect=["GPT-5.6", "GPT-6"]) as sonda:
            with mock.patch.object(api, "_now", return_value=1000.0):
                api.modelo_do_orfao("codex", "D:\\x")
            with mock.patch.object(api, "_now",
                                   return_value=1000.0 + api.MODELOS_TTL_S - 1):
                self.assertEqual(api.modelo_do_orfao("codex", "D:\\x"),
                                 "GPT-5.6")
        sonda.assert_called_once()

    def test_defeito_na_sonda_nao_derruba_o_status(self):
        # Os modulos prometem nao levantar. Este teste trava o cinto de
        # seguranca: a promessa quebrada nao pode apagar o painel inteiro.
        with mock.patch.object(api.modelos, "modelo_codex",
                               side_effect=RuntimeError("boom")):
            self.assertIsNone(api.modelo_do_orfao("codex", "D:\\x"))

    def test_defeito_na_sonda_deixa_rastro_no_contador(self):
        """Engolir a excecao em silencio faria o defeito virar um "—"
        indistinguivel do "nao achei nada". O contador e o que separa os dois,
        e o /health e onde ele aparece — `print` nao escreve nada sob pythonw."""
        antes = api._sondas_falhas
        with mock.patch.object(api.modelos, "modelo_codex",
                               side_effect=RuntimeError("boom")):
            self.assertIsNone(api.modelo_do_orfao("codex", "D:\\explode"))
        self.assertEqual(api._sondas_falhas, antes + 1)

    def test_sonda_que_nao_acha_nada_nao_conta_como_falha(self):
        # O caso NORMAL: um pane de Codex que ainda nao gravou rollout. Contar
        # isto como falha apagaria a unica informacao que o contador carrega.
        antes = api._sondas_falhas
        with mock.patch.object(api.modelos, "modelo_codex", return_value=None):
            self.assertIsNone(api.modelo_do_orfao("codex", "D:\\vazio"))
        self.assertEqual(api._sondas_falhas, antes)

    def test_cache_de_modelos_nao_cresce_sem_limite(self):
        # Uma entrada por (agente, cwd) VISTO, e o servidor roda por meses.
        velho = api._now() - api.MODELOS_TTL_S - 1
        with api._cache_lock:
            for i in range(api.MODELOS_CACHE_MAX + 5):
                api._modelos_cache[("codex", f"D:\\morto{i}")] = {
                    "ts": velho, "valor": None}
            antes = len(api._modelos_cache)
        with mock.patch.object(api.modelos, "modelo_codex", return_value="X"):
            api.modelo_do_orfao("codex", "D:\\vivo")
        self.assertLess(len(api._modelos_cache), antes)
        # A entrada nova sobrevive a propria poda que ela disparou.
        self.assertEqual(api._modelos_cache[("codex", "D:\\vivo")]["valor"], "X")


class TesteHealth(unittest.TestCase):
    """`/health` precisa carregar o estado do sensor do herdr e as ultimas
    divergencias — o unico lugar onde essa informacao sobrevive em producao,
    porque o `print()` de boot e o de `fundir()` nao escrevem nada sob
    pythonw.exe destacado (sys.stdout is None)."""

    def setUp(self):
        api._divergencia_log.clear()
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}
        herdr._binario_ativo = None

    def tearDown(self):
        api._divergencia_log.clear()
        herdr._retrato = {"online": False, "agents": [], "ts": 0.0}
        herdr._binario_ativo = None

    def _get(self, path):
        handler = api.Handler.__new__(api.Handler)   # sem __init__: sem socket
        handler.path = path
        captured = {}
        handler._send = lambda code, payload: captured.update(
            code=code, payload=payload)
        handler.do_GET()
        return captured

    def test_health_expoe_status_do_herdr_e_divergencias(self):
        herdr._binario_ativo = r"C:\herdr\bin\herdr.exe"
        herdr._aplicar([dele("w0:p1", "working")])
        api._divergencia_log.append({
            "sid": "s1", "nosso": "working", "herdr": "idle",
            "pane": "w0:p1", "ts": 123.0,
        })

        captured = self._get("/health")

        self.assertEqual(captured["code"], 200)
        payload = captured["payload"]
        self.assertTrue(payload["ok"])
        self.assertIn("herdr", payload)
        self.assertEqual(payload["herdr"]["binario"], r"C:\herdr\bin\herdr.exe")
        self.assertTrue(payload["herdr"]["online"])
        self.assertIn("divergencias", payload)
        self.assertEqual(len(payload["divergencias"]), 1)
        self.assertEqual(payload["divergencias"][0]["sid"], "s1")

    def test_health_sem_herdr_instalado(self):
        captured = self._get("/health")
        payload = captured["payload"]
        self.assertIsNone(payload["herdr"]["binario"])
        self.assertFalse(payload["herdr"]["online"])
        self.assertEqual(payload["divergencias"], [])

    def test_health_publica_o_contador_de_falhas_da_sonda(self):
        # Sem isto o contador existiria e ninguem o leria: sob pythonw.exe
        # destacado o `print` nao escreve nada, e o /health e o unico lugar de
        # onde se pergunta se a sonda de modelo esta quebrada.
        with mock.patch.object(api.modelos, "modelo_codex",
                               side_effect=RuntimeError("boom")):
            api.modelo_do_orfao("codex", "D:\\health")
        payload = self._get("/health")["payload"]
        self.assertGreaterEqual(payload["sondas_falhas"], 1)


class TesteTag(unittest.TestCase):
    """Quem se identifica e o SERVICO, e nao o cartao da placa."""

    def test_tag_da_env_var_ganha_do_hostname(self):
        with mock.patch.dict(os.environ, {"CLAWD_TAG": "WIN"}):
            self.assertEqual(api.tag_da_maquina(), "WIN")

    def test_tag_e_cortada_em_tres_e_maiuscula(self):
        with mock.patch.dict(os.environ, {"CLAWD_TAG": "notebook"}):
            self.assertEqual(api.tag_da_maquina(), "NOT")

    def test_sem_env_var_usa_o_hostname_sem_pontuacao(self):
        # "DESKTOP-EXEMPLO" -> "DES": o hifen sairia no meio de tres
        # caracteres e comeria um terco do chip sem informar nada.
        with mock.patch.dict(os.environ, {}, clear=True):
            with mock.patch.object(api.platform, "node", return_value="PC-CASA"):
                self.assertEqual(api.tag_da_maquina(), "PCC")

    def test_status_publica_a_tag(self):
        # Vale para os dois retornos: com sessao viva e sem nenhuma.
        self.assertIn("tag", api.build_status())
        api._sessions["s1"] = {
            "data": {"context_pct": 10}, "ts": api._now(),
            "state_ts": api._now(), "state": "working",
        }
        self.assertEqual(api.build_status()["tag"], api.TAG)


class TesteMotor(unittest.TestCase):
    """Com o herdr mandando, a API para de deduzir. Sem ele, deduz como sempre."""

    def setUp(self):
        motor.reset()
        self._sessoes = dict(api._sessions)

    def tearDown(self):
        motor.reset()
        api._sessions.clear()
        api._sessions.update(self._sessoes)

    def _sobe(self, alvo):
        for t in (1.0, 2.0, 3.0):
            motor.avaliar(alvo == motor.HERDR, t)

    def transcript_interrompido(self):
        """Um transcript com a marca e SEM `assistant` depois — o que o motor de
        hooks classifica como turno interrompido."""
        fd, caminho = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(ASSIST_INT + "\n" + MARCA_INT + "\n")
        self.addCleanup(lambda: os.path.exists(caminho) and os.unlink(caminho))
        return caminho

    def test_com_motor_herdr_nao_deduz(self):
        # Uma sessao que o motor de hooks declararia interrompida: marca no
        # transcript, silencio de API alem da carencia. Com o herdr mandando,
        # nada disso e consultado.
        self._sobe(motor.HERDR)
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.transcript_interrompido()}}
        self.assertIsNone(api.corrigir_estado_se_couber(rec, 1e9))

    def test_com_motor_hooks_deduz_como_sempre(self):
        self._sobe(motor.HOOKS)
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.transcript_interrompido()}}
        self.assertEqual(api.corrigir_estado_se_couber(rec, 1e9), "interrompido")

    def test_status_traz_o_motor_nos_dois_casos(self):
        self._sobe(motor.HERDR)
        self.assertEqual(api.build_status()["motor"], motor.HERDR)
        motor.reset()
        self.assertEqual(api.build_status()["motor"], motor.HOOKS)

    def test_health_traz_desde_e_trocas(self):
        self._sobe(motor.HERDR)
        h = api.build_health()
        self.assertEqual(h["motor"]["atual"], motor.HERDR)
        self.assertEqual(h["motor"]["trocas"], 1)
        self.assertEqual(h["motor"]["desde"], 3.0)

    def test_troca_de_motor_nao_perde_o_estado_corrente_das_sessoes(self):
        # A escolha de motor decide QUEM FALA por uma sessao (herdr ou
        # deducao). Nao deve mexer no que ja esta guardado nela.
        sid1, rec1 = sessao("aaaa1111", estado=api.WORKING, repo="um")
        sid2, rec2 = sessao("bbbb2222", estado=api.BLOCKED, repo="dois")
        api._sessions.clear()
        api._sessions[sid1] = rec1
        api._sessions[sid2] = rec2
        # tearDown ja restaura o backup de setUp — nao precisa de cleanup aqui.

        self._sobe(motor.HERDR)
        estados_antes = {sid: rec["state"] for sid, rec in api._sessions.items()}
        ids_antes = {a["session_id"] for a in api.build_status()["labels"]}

        self._sobe(motor.HOOKS)   # troca completa: HERDR -> HOOKS
        self._sobe(motor.HERDR)   # e de volta: HOOKS -> HERDR

        estados_depois = {sid: rec["state"] for sid, rec in api._sessions.items()}
        ids_depois = {a["session_id"] for a in api.build_status()["labels"]}

        self.assertEqual(estados_antes, estados_depois)
        self.assertEqual({sid1: api.WORKING, sid2: api.BLOCKED}, estados_depois)
        self.assertEqual(ids_antes, ids_depois)
        self.assertEqual(ids_depois, {sid1[:8], sid2[:8]})


if __name__ == "__main__":
    unittest.main(verbosity=2)
