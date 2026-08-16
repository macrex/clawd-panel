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


def dele(pane, estado, agente="claude", repo="x"):
    """Um agente como o herdr o devolve, já passado por `parse_agentes`."""
    return {"pane_id": pane, "state": estado, "agent": agente,
            "cwd": "/tmp/" + repo, "repo": repo, "focused": False, "name": ""}


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
