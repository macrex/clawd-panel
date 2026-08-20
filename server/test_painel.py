"""O /status enxuto: sai do fio o que o firmware nao le."""

import copy
import json
import unittest

import painel


def status_cheio():
    """Um /status com todos os blocos, no formato que a API publica."""
    return {
        "online": True,
        "sessions": 2,
        "sessions_active": 1,
        "blocked": 0,
        "done": 1,
        "motor": "hooks",
        "cleaning": False,
        "tag": "WIN",
        "model": "Opus 5",
        "effort": "High",
        "context_repo": "esp32-s3",
        "context_branch": "main",
        "context_pct": 9,
        "session_pct": 47,
        "session_resets_in": 8700,
        "session_resets_hm": "2h25m",
        "session_resets_clock": "2:30am",
        "session_label": "Session 47% - reseta 2:30am",
        "week_pct": 3,
        "week_resets_in": 599000,
        "week_resets_dh": "6d22h",
        "week_resets_date": "22/08/2026 (Sabado)",
        "week_label": "Week 3%",
        "limits_fresh": True,
        "session_known": True,
        "week_known": True,
        "session_inferred": False,
        "updated_ago": 1,
        "herdr_online": True,
        "labels": [
            {
                "session_id": "abc12345",
                "repo": "esp32-s3",
                "branch": "main",
                "agent": "claude",
                "model": "Opus 5",
                "effort": "High",
                "context_pct": 9,
                "color": "green",
                "age": 1,
                "stale": False,
                "state": "working",
                "done": False,
                "cost_usd": 3.75,
                "lines_added": 120,
                "lines_removed": 8,
                "api_ms": 4200,
                "pane_id": "%12",
                "state_age": 3,
                "event": "PostToolUse",
                "proc_alive": True,
                "line": "❖ esp32-s3  ⎇ main | Opus 5 (1M) Max | Context 9%",
            }
        ],
        "clock": {"time": "00:04", "date": "16/08", "weekday": "SAB",
                  "epoch": 1786848698},
        "weather": {"temp": 21, "tmin": 17, "tmax": 28, "text": "limpo",
                    "city": "Sao Paulo", "age": 300, "code": 0, "night": True},
        "works": {"trabalhos": 12, "seconds": 4200, "blocked_seconds": 600,
                  "mediana_seconds": 300, "cost_usd": 3.75,
                  "api_seconds": 900, "lines_added": 120, "lines_removed": 8,
                  "blocks": 2, "mediana_gap_seconds": 45, "marcados": 3},
        "uso": {"cost_usd": 3.75,
                "modelos": [{"rotulo": "Opus 5", "cost_usd": 3.0, "id": "opus-5",
                             "tokens": 91000, "turnos": 40, "estimado": False}],
                "estimados": 0, "fator": 1.0, "vigente": "2026-08", "dia": "hoje"},
        "vitalicio": {"turnos": 4210, "cost_usd": 812.5, "desde": "2026-06-01"},
        "bloqueio": None,
        "captura": None,
        "arquivo": None,
        "colors": {"context": "green", "session": "yellow", "week": "green"},
    }


class TestEnxugar(unittest.TestCase):
    def test_tira_o_que_o_firmware_ignora(self):
        magro = painel.enxugar(status_cheio())
        for chave in ("sessions_active", "blocked", "session_label",
                      "week_label", "herdr_online"):
            self.assertNotIn(chave, magro, chave)

        agente = magro["labels"][0]
        for chave in ("event", "proc_alive", "line"):
            self.assertNotIn(chave, agente, chave)
        # O firmware aprendeu a ler o state_age (turno cronometrado na tela 1
        # em pe); a regra do modulo e tirar da lista o que a placa passa a ler.
        self.assertEqual(3, agente["state_age"])

        for chave in ("api_seconds", "blocks", "mediana_gap_seconds", "marcados"):
            self.assertNotIn(chave, magro["works"], chave)

        for chave in ("estimados", "fator", "vigente", "dia"):
            self.assertNotIn(chave, magro["uso"], chave)

        for chave in ("tokens", "estimado"):
            self.assertNotIn(chave, magro["uso"]["modelos"][0], chave)

        for chave in ("code", "night"):
            self.assertNotIn(chave, magro["weather"], chave)

    def test_preserva_tudo_o_que_o_firmware_le(self):
        cheio = status_cheio()
        magro = painel.enxugar(cheio)

        # Topo: cada chave que sobrou tem que ter o MESMO valor.
        for chave, valor in magro.items():
            if chave in ("labels", "works", "uso", "weather"):
                continue
            self.assertEqual(valor, cheio[chave], chave)

        # O agente inteiro, menos os quatro campos mortos.
        agente, original = magro["labels"][0], cheio["labels"][0]
        for chave in ("session_id", "repo", "branch", "agent", "model", "effort",
                      "context_pct", "color", "age", "stale", "state", "done",
                      "cost_usd", "lines_added", "lines_removed", "api_ms",
                      "pane_id"):
            self.assertEqual(agente[chave], original[chave], chave)

        # O epoch NAO sai: e dele que a placa acerta o relogio quando o SNTP
        # nao alcanca a internet.
        self.assertEqual(magro["clock"]["epoch"], 1786848698)
        self.assertEqual(magro["clock"]["time"], "00:04")

        # Os prazos em SEGUNDOS ficam: e deles que a placa deriva tudo o mais.
        # O texto pronto ("2h25m", "1:59am") saiu quando ela aprendeu a contar
        # sozinha — ver TestTempo.
        self.assertEqual(magro["session_resets_in"], 8700)
        self.assertEqual(magro["week_resets_in"], 599000)

        # E o livro-caixa, que alimenta o card HOJE e o nivel do Clawd.
        self.assertEqual(magro["works"]["trabalhos"], 12)
        self.assertEqual(magro["works"]["lines_added"], 120)
        self.assertEqual(magro["uso"]["modelos"][0]["rotulo"], "Opus 5")
        self.assertEqual(magro["uso"]["modelos"][0]["id"], "opus-5")
        self.assertEqual(magro["vitalicio"]["turnos"], 4210)

    def test_nao_mexe_no_original(self):
        # `build_status` monta o dicionario uma vez; enxugar nao pode consumi-lo.
        cheio = status_cheio()
        antes = copy.deepcopy(cheio)
        painel.enxugar(cheio)
        self.assertEqual(cheio, antes)

    def test_encolhe_de_verdade(self):
        cheio = status_cheio()
        a = len(json.dumps(cheio, ensure_ascii=False))
        b = len(json.dumps(painel.enxugar(cheio), ensure_ascii=False))
        self.assertLess(b, a)
        # Medido no payload real: ~21% com um agente. O teste guarda o piso, e
        # nao o numero exato, porque ele cresce com a quantidade de agentes.
        self.assertGreater((a - b) / a, 0.15)

    def test_blocos_ausentes_nao_quebram(self):
        # Painel vazio: `weather` null, sem `uso`, sem `works`. A API ja publica
        # assim, e enxugar nao pode inventar chave nem estourar.
        magro = painel.enxugar({"online": False, "sessions": 0, "weather": None,
                                "labels": [], "bloqueio": None})
        self.assertIsNone(magro["weather"])
        self.assertEqual(magro["labels"], [])
        self.assertNotIn("works", magro)

    def test_chave_nova_da_api_atravessa(self):
        # A lista e de REMOCAO, e nao de permissao, de proposito: um campo novo
        # que o firmware aprenda a ler chega sozinho. Com lista de permissao ele
        # sumiria em silencio, que e o defeito caro.
        magro = painel.enxugar({"campo_que_ainda_nao_existe": 42, "labels": []})
        self.assertEqual(magro["campo_que_ainda_nao_existe"], 42)


class TestTempo(unittest.TestCase):
    """O texto de tempo sai; os segundos ficam."""

    def test_os_quatro_textos_derivados_saem(self):
        magro = painel.enxugar(status_cheio())
        for morto in ("session_resets_hm", "session_resets_clock",
                      "week_resets_dh", "week_resets_date"):
            self.assertNotIn(morto, magro)

    def test_os_segundos_ficam(self):
        # E deles que a placa deriva tudo — cortar os dois deixaria o painel sem
        # prazo nenhum.
        magro = painel.enxugar(status_cheio())
        self.assertEqual(magro["session_resets_in"], 8700)
        self.assertEqual(magro["week_resets_in"], 599000)

    def test_o_relogio_do_cabecalho_fica(self):
        # `clock` e outra coisa: e a hora AGORA, e a placa a usa como reserva
        # ate o SNTP chegar.
        magro = painel.enxugar(status_cheio())
        self.assertIn("clock", magro)


class TestPedido(unittest.TestCase):
    def test_reconhece_o_pedido(self):
        self.assertTrue(painel.pedido_do_painel("/status?campos=painel"))
        self.assertTrue(painel.pedido_do_painel("/status?x=1&campos=painel"))
        self.assertTrue(painel.pedido_do_painel("/status?campos=painel&x=1"))

    def test_ignora_o_resto(self):
        self.assertFalse(painel.pedido_do_painel("/status"))
        self.assertFalse(painel.pedido_do_painel("/status?"))
        self.assertFalse(painel.pedido_do_painel("/status?campos="))
        self.assertFalse(painel.pedido_do_painel("/status?outro=painel"))
        # Valor desconhecido de um cliente futuro nao ativa um corte que ele nao
        # entende — a comparacao e por igualdade, nao por presenca da chave.
        self.assertFalse(painel.pedido_do_painel("/status?campos=tudo"))
        self.assertFalse(painel.pedido_do_painel("/status?campos=painel2"))


class TestFrio(unittest.TestCase):
    """Os blocos que mudam devagar e nao precisam viajar a cada dois segundos."""

    def test_o_resumo_publicado_sempre(self):
        # Sem `?frio=` o documento sai inteiro, COM o resumo junto: e como a
        # placa aprende o valor da primeira vez.
        saida = painel.aplicar_frio(status_cheio(), "/status?campos=painel")
        self.assertIn("frio", saida)
        self.assertIn("works", saida)
        self.assertIn("uso", saida)
        self.assertIn("vitalicio", saida)

    def test_resumo_igual_omite_os_blocos(self):
        cheio = status_cheio()
        resumo = painel.resumo_frio(cheio)
        saida = painel.aplicar_frio(cheio, f"/status?campos=painel&frio={resumo}")
        self.assertEqual(saida["frio"], resumo)
        for bloco in painel.FRIOS:
            self.assertNotIn(bloco, saida)
        # O resto do documento continua inteiro: o corte e de UMA parte, e nao
        # da resposta — 87% dela muda a cada poll.
        self.assertEqual(saida["session_pct"], cheio["session_pct"])
        self.assertEqual(saida["labels"], cheio["labels"])

    def test_resumo_velho_traz_os_blocos_de_volta(self):
        saida = painel.aplicar_frio(status_cheio(), "/status?frio=aaaaaaaaaaaa")
        self.assertIn("works", saida)
        self.assertIn("vitalicio", saida)

    def test_conteudo_que_muda_muda_o_resumo(self):
        # A invalidacao e automatica e e o coracao disto: um turno novo muda
        # `works`, muda o resumo, e o bloco volta a viajar sem ninguem lembrar de
        # expirar nada. A virada do dia entra por esta mesma porta.
        antes = status_cheio()
        depois = copy.deepcopy(antes)
        depois["works"]["trabalhos"] += 1
        self.assertNotEqual(painel.resumo_frio(antes), painel.resumo_frio(depois))

        # E o mesmo conteudo da o mesmo resumo, senao o bloco viajaria sempre.
        self.assertEqual(painel.resumo_frio(antes),
                         painel.resumo_frio(copy.deepcopy(antes)))

    def test_o_resumo_nao_depende_da_ordem_das_chaves(self):
        # A ordem de um dicionario Python nao e contrato. Sem `sort_keys`, duas
        # versoes do servidor dariam resumos diferentes para o mesmo conteudo.
        antes = status_cheio()
        depois = copy.deepcopy(antes)
        depois["works"] = dict(reversed(list(depois["works"].items())))
        self.assertEqual(painel.resumo_frio(antes), painel.resumo_frio(depois))

    def test_bloco_ausente_tem_resumo_proprio(self):
        # Uma API sem livro-caixa (primeiro dia, arquivo vazio) tambem tem um
        # resumo estavel — e ele e DIFERENTE do de um dia com dados, senao a
        # placa guardaria o bloco de ontem achando que nada mudou.
        vazio = {k: v for k, v in status_cheio().items() if k not in painel.FRIOS}
        self.assertTrue(painel.resumo_frio(vazio))
        self.assertNotEqual(painel.resumo_frio(vazio),
                            painel.resumo_frio(status_cheio()))

    def test_nao_altera_o_original(self):
        cheio = status_cheio()
        copia = copy.deepcopy(cheio)
        painel.aplicar_frio(cheio, f"/status?frio={painel.resumo_frio(cheio)}")
        self.assertEqual(cheio, copia)

    def test_convive_com_o_enxugar(self):
        # A ordem importa: `enxugar` tira campos de dentro de `works` e `uso`,
        # entao o resumo tem que sair do documento JA enxuto. Calculado antes, a
        # placa guardaria um resumo que nunca mais bateria.
        magro = painel.enxugar(status_cheio())
        resumo = painel.resumo_frio(magro)
        saida = painel.aplicar_frio(magro, f"/status?campos=painel&frio={resumo}")
        self.assertNotIn("works", saida)


if __name__ == "__main__":
    unittest.main()
