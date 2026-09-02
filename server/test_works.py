#!/usr/bin/env python3
"""Testes do livro-caixa. Só stdlib — `python test_works.py`.

O que estas provas seguram, em ordem de importância:

  1. Um trabalho que não terminou normalmente TEM que aparecer, marcado. Se ele
     for descartado, some do livro-caixa justamente o caso interessante; se for
     contado como turno normal, envenena toda média.
  2. Contador que regride nunca vira delta negativo.
  3. Nada aqui levanta exceção para fora — a contabilidade não pode derrubar o
     painel.
"""

import json
import os
import tempfile
import time
import unittest

import works


class TestAbrirFechar(unittest.TestCase):

    def dados(self, custo, api_ms, mais=0, menos=0, **extra):
        d = {"cost_usd": custo, "api_ms": api_ms,
             "lines_added": mais, "lines_removed": menos}
        d.update(extra)
        return d

    def test_delta_e_a_diferenca_entre_os_dois_snapshots(self):
        # Os contadores são cumulativos da SESSÃO. O que este trabalho consumiu
        # é a diferença — sem ler transcript nenhum.
        t = works.abrir("s1", self.dados(10.0, 5000, 100, 10), now=1000.0)
        reg = works.fechar(t, self.dados(10.5, 9000, 137, 22), now=1167.7)
        self.assertAlmostEqual(reg["cost_usd"], 0.5)
        self.assertAlmostEqual(reg["api_seconds"], 4.0)
        self.assertEqual(reg["lines_added"], 37)
        self.assertEqual(reg["lines_removed"], 12)
        self.assertAlmostEqual(reg["seconds"], 167.7)

    def test_identidade_e_a_do_inicio_do_trabalho(self):
        # Repo e modelo podem mudar no meio de uma sessão longa. O que descreve
        # este trabalho é o que valia quando ele começou.
        t = works.abrir("s1", self.dados(0, 0, repo="esp32-s3", model="Opus 5"),
                        now=1000.0)
        reg = works.fechar(t, self.dados(0, 0, repo="outro", model="Haiku"),
                           now=1010.0)
        self.assertEqual(reg["repo"], "esp32-s3")
        self.assertEqual(reg["model"], "Opus 5")

    def test_contador_que_regride_nao_vira_negativo(self):
        # Acontece quando os totais zeram. Um delta negativo envenenaria
        # qualquer soma, e um número inventado seria pior — daí a marca.
        t = works.abrir("s1", self.dados(10.0, 9000, 500, 90), now=1000.0)
        reg = works.fechar(t, self.dados(0.2, 300, 4, 1), now=1060.0)
        self.assertEqual(reg["cost_usd"], 0)
        self.assertEqual(reg["api_seconds"], 0)
        self.assertEqual(reg["lines_added"], 0)
        self.assertTrue(reg["reset"])

    def test_contador_ausente_sai_nulo_e_nao_zero(self):
        # Statusline que não publica o campo: não se sabe o custo. Zero seria
        # uma afirmação.
        t = works.abrir("s1", {"api_ms": 1000}, now=1000.0)
        reg = works.fechar(t, {"api_ms": 3000}, now=1010.0)
        self.assertIsNone(reg["cost_usd"])
        self.assertAlmostEqual(reg["api_seconds"], 2.0)

    def test_turno_normal_nao_leva_marca_nenhuma(self):
        # A ausência é a informação: um registro sem marca terminou como devia.
        t = works.abrir("s1", self.dados(0, 0), now=1000.0)
        reg = works.fechar(t, self.dados(0, 0), now=1010.0)
        for marca in ("partial", "aborted", "interrupted", "stalled", "reset"):
            self.assertNotIn(marca, reg)

    def test_motivo_marca_o_registro(self):
        for motivo in ("partial", "aborted", "interrupted", "stalled"):
            t = works.abrir("s1", self.dados(0, 0), now=1000.0)
            reg = works.fechar(t, self.dados(0, 0), now=1010.0, motivo=motivo)
            self.assertTrue(reg[motivo], motivo)

    def test_trabalho_invalido_nao_levanta(self):
        for lixo in (None, {}, "trabalho", 42, {"started": "ontem"}):
            self.assertIsNone(works.fechar(lixo, {}, now=1.0), repr(lixo))


class TestBloqueio(unittest.TestCase):

    def test_bloqueio_conta_na_ABERTURA(self):
        # Contado no desbloqueio, um bloqueio que nunca foi resolvido sumiria do
        # registro — justamente o caso mais interessante.
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.bloquear(t, api_ms=100, now=1010.0)
        self.assertEqual(t["blocks"], 1)

    def test_intervalo_bloqueado_entra_no_registro(self):
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.bloquear(t, api_ms=100, now=1010.0)
        works.desbloquear(t, api_ms=900, now=1051.0)
        reg = works.fechar(t, {"api_ms": 900}, now=1100.0)
        self.assertAlmostEqual(reg["blocked_seconds"], 41.0)
        self.assertEqual(reg["blocks"], 1)

    def test_api_parada_nao_desbloqueia(self):
        # A prova de que você respondeu é `api_ms` ter andado. Sem isso, o
        # heartbeat sozinho declararia o desbloqueio a cada 10 s.
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.bloquear(t, api_ms=500, now=1010.0)
        works.desbloquear(t, api_ms=500, now=1030.0)
        self.assertIsNotNone(t["blocked_start"])
        self.assertEqual(t["blocked_total"], 0.0)

    def test_bloquear_duas_vezes_nao_conta_duas(self):
        # O heartbeat reafirma o bloqueio a cada ciclo; é o mesmo bloqueio.
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.bloquear(t, api_ms=100, now=1010.0)
        works.bloquear(t, api_ms=100, now=1020.0)
        self.assertEqual(t["blocks"], 1)
        self.assertEqual(t["blocked_start"], 1010.0)

    def test_bloqueio_aberto_no_fechamento_e_somado_ate_ali(self):
        # Ele aconteceu. Descartar faria o tempo de espera sumir exatamente do
        # trabalho em que ele mais pesou.
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.bloquear(t, api_ms=100, now=1010.0)
        reg = works.fechar(t, {"api_ms": 100}, now=1070.0, motivo="aborted")
        self.assertAlmostEqual(reg["blocked_seconds"], 60.0)
        self.assertEqual(reg["blocks"], 1)

    def test_dois_bloqueios_no_mesmo_trabalho_somam(self):
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.bloquear(t, api_ms=100, now=1010.0)
        works.desbloquear(t, api_ms=200, now=1020.0)
        works.bloquear(t, api_ms=200, now=1030.0)
        works.desbloquear(t, api_ms=300, now=1055.0)
        reg = works.fechar(t, {"api_ms": 300}, now=1060.0)
        self.assertAlmostEqual(reg["blocked_seconds"], 35.0)
        self.assertEqual(reg["blocks"], 2)

    def test_desbloquear_sem_bloqueio_nao_faz_nada(self):
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.desbloquear(t, api_ms=900, now=1010.0)
        self.assertEqual(t["blocked_total"], 0.0)

    def test_entradas_invalidas_nao_levantam(self):
        for lixo in (None, "x", 42):
            works.bloquear(lixo, 1, 1.0)
            works.desbloquear(lixo, 1, 1.0)


class TestSemear(unittest.TestCase):
    """A base que só aparece depois da abertura.

    No primeiro turno de uma sessão nova o `UserPromptSubmit` chega antes do
    primeiro heartbeat da statusline. Sem semear, esse turno — que costuma ser o
    maior — sai com custo, linhas e tempo de API nulos.
    """

    def test_base_vazia_e_adotada_do_primeiro_heartbeat(self):
        t = works.abrir("s1", {}, now=1000.0)
        works.semear(t, {"cost_usd": 0.02, "api_ms": 500,
                         "lines_added": 0, "lines_removed": 0,
                         "repo": "esp32-s3", "model": "Opus 5"})
        reg = works.fechar(t, {"cost_usd": 0.42, "api_ms": 8500,
                               "lines_added": 37, "lines_removed": 12},
                           now=1100.0)
        self.assertAlmostEqual(reg["cost_usd"], 0.4)
        self.assertAlmostEqual(reg["api_seconds"], 8.0)
        self.assertEqual(reg["lines_added"], 37)
        self.assertEqual(reg["repo"], "esp32-s3")
        self.assertEqual(reg["model"], "Opus 5")

    def test_base_existente_nunca_e_substituida(self):
        # Trocá-la por uma mais nova apagaria o começo do turno.
        t = works.abrir("s1", {"cost_usd": 1.0, "api_ms": 1000}, now=1000.0)
        works.semear(t, {"cost_usd": 9.0, "api_ms": 9000})
        reg = works.fechar(t, {"cost_usd": 1.5, "api_ms": 3000}, now=1010.0)
        self.assertAlmostEqual(reg["cost_usd"], 0.5)
        self.assertAlmostEqual(reg["api_seconds"], 2.0)

    def test_semear_com_nada_nao_faz_nada(self):
        t = works.abrir("s1", {}, now=1000.0)
        for lixo in (None, {}, ""):
            works.semear(t, lixo)
        self.assertIsNone((t["snap"] or {}).get("cost_usd"))

    def test_entrada_invalida_nao_levanta(self):
        for lixo in (None, "x", 42):
            works.semear(lixo, {"cost_usd": 1})


class TestVao(unittest.TestCase):
    """O maior intervalo sem chamada de API dentro do trabalho.

    Medido em 98 turnos reais: vão mediano de 1min02, p90 de 2min30 — e o turno
    mais longo de todos (16h17) tinha um vão único de 11h55, 73% da duração
    dele. Sem este número aquele turno entra no livro-caixa como "demorou 16
    horas", quando o que houve foi ficar parado a noite inteira.
    """

    def test_heartbeat_sem_atividade_nao_fecha_o_vao(self):
        # `api_ms` parado prova que nada aconteceu no intervalo.
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.atividade(t, api_ms=100, now=1030.0)
        works.atividade(t, api_ms=100, now=1060.0)
        self.assertEqual(t["maior_vao"], 0.0)
        self.assertEqual(t["ultima_api"], 1000.0)

    def test_atividade_fecha_o_vao_e_guarda_o_maior(self):
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.atividade(t, api_ms=100, now=1030.0)      # parado
        works.atividade(t, api_ms=500, now=1040.0)      # andou: vao de 40 s
        works.atividade(t, api_ms=900, now=1055.0)      # vao de 15 s, menor
        self.assertAlmostEqual(t["maior_vao"], 40.0)

    def test_vao_aberto_no_fechamento_conta(self):
        # O turno que termina logo depois de doze horas parado.
        t = works.abrir("s1", {"api_ms": 100}, now=1000.0)
        works.atividade(t, api_ms=500, now=1010.0)
        reg = works.fechar(t, {"api_ms": 500}, now=1010.0 + 11 * 3600)
        self.assertAlmostEqual(reg["gap_seconds"], 11 * 3600.0)

    def test_turno_corrido_tem_vao_pequeno(self):
        t = works.abrir("s1", {"api_ms": 0}, now=1000.0)
        for i in range(1, 6):
            works.atividade(t, api_ms=i * 100, now=1000.0 + i * 5)
        reg = works.fechar(t, {"api_ms": 500}, now=1025.0)
        self.assertAlmostEqual(reg["gap_seconds"], 5.0)

    def test_entrada_invalida_nao_levanta(self):
        for lixo in (None, "x", 42):
            works.atividade(lixo, 1, 1.0)


class TestArquivo(unittest.TestCase):

    def setUp(self):
        fd, self.caminho = tempfile.mkstemp(suffix=".jsonl")
        os.close(fd)
        os.unlink(self.caminho)          # o módulo cria ao gravar

    def tearDown(self):
        if os.path.exists(self.caminho):
            os.unlink(self.caminho)

    def test_grava_e_le_de_volta(self):
        self.assertTrue(works.gravar({"session_id": "s1", "seconds": 10}, self.caminho))
        self.assertTrue(works.gravar({"session_id": "s2", "seconds": 20}, self.caminho))
        regs = works.ler(self.caminho)
        self.assertEqual([r["session_id"] for r in regs], ["s1", "s2"])

    def test_arquivo_que_nao_existe_devolve_lista_vazia(self):
        self.assertEqual(works.ler(os.path.join(self.caminho, "nao", "existe")), [])

    def test_linha_corrompida_e_pulada_e_nao_derruba_o_resto(self):
        # Uma queda de energia no meio de uma escrita não pode apagar os mil
        # registros intactos que estão acima dela.
        with open(self.caminho, "w", encoding="utf-8") as f:
            f.write(json.dumps({"session_id": "bom1"}) + "\n")
            f.write('{"session_id": "trunc\n')
            f.write("\n")
            f.write("[1,2,3]\n")          # JSON válido, mas não é um registro
            f.write(json.dumps({"session_id": "bom2"}) + "\n")
        regs = works.ler(self.caminho)
        self.assertEqual([r["session_id"] for r in regs], ["bom1", "bom2"])

    def test_limite_devolve_os_ultimos(self):
        for i in range(5):
            works.gravar({"session_id": f"s{i}"}, self.caminho)
        regs = works.ler(self.caminho, limite=2)
        self.assertEqual([r["session_id"] for r in regs], ["s3", "s4"])

    def test_erro_de_escrita_devolve_falso_em_vez_de_levantar(self):
        # Caminho impossível: disco cheio e permissão negada chegam pelo mesmo
        # caminho de código. Isto NÃO pode subir para o /ingest.
        self.assertFalse(works.gravar({"a": 1}, os.path.join(self.caminho, "x", "y")))

    def test_registro_nao_serializavel_devolve_falso(self):
        self.assertFalse(works.gravar({"x": object()}, self.caminho))
        self.assertFalse(works.gravar("nao sou dict", self.caminho))


class TestResumo(unittest.TestCase):

    def regs(self, *duracoes, **extra):
        return [dict({"seconds": d, "cost_usd": 0.1, "api_seconds": 1.0,
                      "blocked_seconds": 2.0, "lines_added": 3,
                      "lines_removed": 1, "blocks": 1}, **extra)
                for d in duracoes]

    def test_soma_e_contagem(self):
        r = works.resumo(self.regs(10.0, 20.0, 30.0), now=0)
        self.assertEqual(r["trabalhos"], 3)
        self.assertAlmostEqual(r["seconds"], 60.0)
        self.assertAlmostEqual(r["cost_usd"], 0.3)
        self.assertEqual(r["lines_added"], 9)
        self.assertEqual(r["blocks"], 3)

    def test_mediana_resiste_ao_trabalho_de_tres_horas(self):
        # O caso real: um turno cujo `Stop` se perdeu fica aberto e entra com
        # horas. A média iria a 45 min e não diria nada sobre um turno típico.
        r = works.resumo(self.regs(60.0, 90.0, 120.0, 10800.0), now=0)
        self.assertAlmostEqual(r["mediana_seconds"], 105.0)

    def test_marcados_sao_contados_a_parte(self):
        regs = self.regs(10.0) + self.regs(20.0, interrupted=True) + \
            self.regs(30.0, stalled=True)
        r = works.resumo(regs, now=0)
        self.assertEqual(r["trabalhos"], 3)      # continuam contando
        self.assertEqual(r["marcados"], 2)

    def test_lista_vazia_nao_levanta(self):
        r = works.resumo([], now=0)
        self.assertEqual(r["trabalhos"], 0)
        self.assertEqual(r["mediana_seconds"], 0.0)

    def test_lixo_na_lista_e_ignorado(self):
        r = works.resumo([None, "x", 42, {"seconds": 10.0}], now=0)
        self.assertEqual(r["trabalhos"], 1)


class TestDia(unittest.TestCase):
    """O recorte de "hoje" e o cache de leitura."""

    def test_meia_noite_e_LOCAL_e_nao_utc(self):
        # Nesta maquina a diferenca e de tres horas: um turno das 22h apareceria
        # como sendo de amanha se a conta fosse em UTC.
        import time as _t
        agora = _t.time()
        inicio = works.inicio_do_dia(agora)
        t = _t.localtime(inicio)
        self.assertEqual((t.tm_hour, t.tm_min, t.tm_sec), (0, 0, 0))
        self.assertEqual(_t.localtime(agora).tm_mday, t.tm_mday)
        self.assertLessEqual(inicio, agora)

    def test_recorta_pelo_INICIO_e_nao_pelo_fim(self):
        # Um turno que atravessa a meia-noite pertence ao dia em que voce o
        # pediu, e nao ao dia em que ele terminou.
        regs = [{"started": 100.0, "ended": 250.0},
                {"started": 200.0, "ended": 300.0}]
        self.assertEqual(len(works.desde(regs, 150.0)), 1)
        self.assertEqual(works.desde(regs, 150.0)[0]["started"], 200.0)

    def test_lixo_na_lista_nao_derruba_o_recorte(self):
        self.assertEqual(works.desde([None, "x", {"started": "ontem"}], 0), [])

    def test_cache_relê_quando_o_arquivo_cresce(self):
        fd, caminho = tempfile.mkstemp(suffix=".jsonl")
        os.close(fd)
        self.addCleanup(os.unlink, caminho)
        works.gravar({"session_id": "a", "started": 1.0}, caminho)
        self.assertEqual(len(works.ler_cache(caminho)), 1)
        works.gravar({"session_id": "b", "started": 2.0}, caminho)
        self.assertEqual(len(works.ler_cache(caminho)), 2)

    def test_cache_de_arquivo_inexistente_e_vazio(self):
        self.assertEqual(works.ler_cache(os.path.join(tempfile.gettempdir(),
                                                      "nao", "existe.jsonl")), [])


class TestTokensNoRegistro(unittest.TestCase):
    """Os campos que o caminho ao vivo passou a ter."""

    def setUp(self):
        import datetime
        # Transcript sintetico com duas respostas dentro da janela do turno.
        # Serializacao COMPACTA, como o Claude Code grava.
        self.t0 = 1785870000.0
        linhas = []
        for offset, modelo, saida in ((10, "claude-opus-5", 100),
                                      (20, "claude-haiku-4-5", 7)):
            ts = datetime.datetime.fromtimestamp(
                self.t0 + offset, datetime.timezone.utc
            ).strftime("%Y-%m-%dT%H:%M:%S.000Z")
            linhas.append(json.dumps(
                {"type": "assistant", "timestamp": ts,
                 "message": {"model": modelo,
                             "usage": {"output_tokens": saida}}},
                separators=(",", ":")))
        fd, self.caminho = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write("\n".join(linhas) + "\n")

    def tearDown(self):
        os.unlink(self.caminho)

    def dados(self, **extra):
        d = {"cost_usd": 1.0, "api_ms": 0, "lines_added": 0,
             "lines_removed": 0, "model": "Opus 5 (1M context)",
             "model_id": "claude-opus-5[1m]", "fast_mode": False,
             "transcript_path": self.caminho}
        d.update(extra)
        return d

    def test_model_id_e_copiado_na_abertura(self):
        t = works.abrir("s1", self.dados(), self.t0)
        reg = works.fechar(t, self.dados(cost_usd=2.0), self.t0 + 60)
        self.assertEqual(reg["model_id"], "claude-opus-5[1m]")

    def test_fast_e_booleano(self):
        t = works.abrir("s1", self.dados(fast_mode=True), self.t0)
        reg = works.fechar(t, self.dados(cost_usd=2.0), self.t0 + 60)
        self.assertIs(reg["fast"], True)

    def test_fast_ausente_vira_falso(self):
        d = self.dados()
        del d["fast_mode"]
        t = works.abrir("s1", d, self.t0)
        reg = works.fechar(t, d, self.t0 + 60)
        self.assertIs(reg["fast"], False)

    def test_tokens_vem_do_transcript(self):
        t = works.abrir("s1", self.dados(), self.t0)
        reg = works.fechar(t, self.dados(cost_usd=2.0), self.t0 + 60)
        self.assertEqual(reg["tokens"]["claude-opus-5"]["output"], 100)
        self.assertEqual(reg["tokens"]["claude-haiku-4-5"]["output"], 7)

    def test_sem_transcript_o_registro_sai_sem_tokens(self):
        d = self.dados()
        d["transcript_path"] = None
        t = works.abrir("s1", d, self.t0)
        reg = works.fechar(t, d, self.t0 + 60)
        self.assertEqual(reg["tokens"], {})
        # E o custo real continua la: perder token nao pode perder dolar.
        self.assertIsNotNone(reg["cost_usd"])

    def test_transcript_ilegivel_nao_derruba_o_fechamento(self):
        d = self.dados()
        d["transcript_path"] = "/nao/existe.jsonl"
        t = works.abrir("s1", d, self.t0)
        reg = works.fechar(t, d, self.t0 + 60)
        self.assertEqual(reg["tokens"], {})


class TestPorModelo(unittest.TestCase):

    def registro(self, tokens, cost_usd=None, fast=False):
        return {"session_id": "s1", "started": 1785870000.0,
                "cost_usd": cost_usd, "fast": fast, "tokens": tokens}

    def test_um_modelo_com_custo_real(self):
        r = self.registro({"claude-opus-5": {"output": 1_000_000}},
                          cost_usd=25.0)
        out = works.por_modelo([r])
        self.assertEqual(len(out["modelos"]), 1)
        m = out["modelos"][0]
        self.assertEqual(m["id"], "claude-opus-5")
        self.assertEqual(m["rotulo"], "Opus 5")
        self.assertAlmostEqual(m["cost_usd"], 25.0, places=4)
        self.assertIs(m["estimado"], False)

    def test_turno_misto_rateia_o_custo_real(self):
        """Tabelado: opus 25.00 + haiku 5.00 = 30.00. Real: 15.00 -> fator 0.5."""
        r = self.registro({"claude-opus-5": {"output": 1_000_000},
                           "claude-haiku-4-5": {"output": 1_000_000}},
                          cost_usd=15.0)
        out = works.por_modelo([r])
        porid = {m["id"]: m for m in out["modelos"]}
        self.assertAlmostEqual(porid["claude-opus-5"]["cost_usd"], 12.5, places=4)
        self.assertAlmostEqual(porid["claude-haiku-4-5"]["cost_usd"], 2.5, places=4)
        # A soma preserva o total autoritativo do Claude Code.
        self.assertAlmostEqual(out["cost_usd"], 15.0, places=4)
        self.assertAlmostEqual(out["fator"], 0.5, places=4)

    def test_sem_custo_real_usa_a_tabela_e_marca_estimado(self):
        r = self.registro({"claude-opus-5": {"output": 1_000_000}})
        out = works.por_modelo([r])
        m = out["modelos"][0]
        self.assertAlmostEqual(m["cost_usd"], 25.0, places=4)
        self.assertIs(m["estimado"], True)
        self.assertEqual(out["estimados"], 1)
        self.assertIsNone(out["fator"])

    def test_ordena_por_custo_decrescente(self):
        r = self.registro({"claude-haiku-4-5": {"output": 1_000_000},
                           "claude-opus-5": {"output": 1_000_000}})
        out = works.por_modelo([r])
        self.assertEqual([m["id"] for m in out["modelos"]],
                         ["claude-opus-5", "claude-haiku-4-5"])

    def test_soma_turnos_e_tokens_entre_registros(self):
        rs = [self.registro({"claude-opus-5": {"output": 100, "input": 5}}),
              self.registro({"claude-opus-5": {"output": 200}})]
        out = works.por_modelo(rs)
        m = out["modelos"][0]
        self.assertEqual(m["turnos"], 2)
        self.assertEqual(m["tokens"], 305)

    def test_modo_rapido_usa_o_preco_dobrado(self):
        r = self.registro({"claude-opus-5": {"output": 1_000_000}}, fast=True)
        out = works.por_modelo([r])
        self.assertAlmostEqual(out["modelos"][0]["cost_usd"], 50.0, places=4)

    def test_dia_habilita_a_promocao(self):
        r = self.registro({"claude-sonnet-5": {"input": 1_000_000}})
        dentro = works.por_modelo([r], dia="2026-08-04")
        fora = works.por_modelo([r], dia="2026-09-01")
        self.assertAlmostEqual(dentro["modelos"][0]["cost_usd"], 2.0, places=4)
        self.assertAlmostEqual(fora["modelos"][0]["cost_usd"], 3.0, places=4)

    def test_modelo_fora_da_tabela_sai_com_custo_nulo(self):
        r = self.registro({"modelo-novo-99": {"output": 1_000_000}})
        out = works.por_modelo([r])
        m = out["modelos"][0]
        self.assertIsNone(m["cost_usd"])
        self.assertEqual(m["tokens"], 1_000_000)

    def test_registro_sem_tokens_e_ignorado(self):
        out = works.por_modelo([{"session_id": "s1", "cost_usd": 3.0}])
        self.assertEqual(out["modelos"], [])
        self.assertAlmostEqual(out["cost_usd"], 0.0, places=4)

    def test_lista_vazia(self):
        out = works.por_modelo([])
        self.assertEqual(out["modelos"], [])
        self.assertIsNone(out["fator"])


class TestFatiaFable(unittest.TestCase):
    """Quanto do gasto veio do Fable — a barra discreta da tela nova.

    Ela existe porque a statusline NAO publica janela de limite por modelo: o
    unico jeito honesto de dizer algo sobre o Fable e falar do gasto que este
    livro-caixa mediu.
    """

    def registro(self, tokens):
        return {"session_id": "s1", "started": 1785870000.0, "tokens": tokens}

    def test_metade_do_custo_e_fable(self):
        # Fable 1M de saida = US$ 50; Opus 5 2M de saida = US$ 50. Metade.
        r = self.registro({"claude-fable-5": {"output": 1_000_000},
                           "claude-opus-5": {"output": 2_000_000}})
        self.assertEqual(works.fatia_fable([r]), 50)

    def test_mythos_conta_como_fable(self):
        """Sao o mesmo modelo por tras; separa-los na tela mentiria."""
        r = self.registro({"claude-mythos-5": {"output": 1_000_000},
                           "claude-opus-5": {"output": 1_000_000}})
        self.assertEqual(works.fatia_fable([r]), 67)

    def test_sem_fable_e_zero_e_nao_nulo(self):
        r = self.registro({"claude-opus-5": {"output": 1_000_000}})
        self.assertEqual(works.fatia_fable([r]), 0)

    def test_sem_custo_conhecido_devolve_nulo(self):
        """Zero diria "nao gastei"; None diz "nao sei", e a barra nem aparece."""
        self.assertIsNone(works.fatia_fable([]))
        self.assertIsNone(works.fatia_fable(
            [self.registro({"modelo-novo-99": {"output": 1_000_000}})]))


class TestVitalicio(unittest.TestCase):
    """O total de TODA a historia, que alimenta o nivel do Clawd.

    A prova que mais importa e a primeira: o custo tem que sair dos TOKENS. No
    livro-caixa real, so 74 de 709 registros tem `cost_usd`; somar aquele campo
    daria US$ 275 onde o certo sao US$ 2.481, e o Clawd apareceria dezenas de
    niveis abaixo do que a historia merece.
    """

    def test_custo_vem_dos_tokens_e_nao_do_campo(self):
        regs = [
            # Sem `cost_usd`: e o caso da MAIORIA. 1M de entrada de Opus 5
            # (US$ 5/MTok) + 1M de saida (US$ 25/MTok) = US$ 30.
            {"started": 1000, "tokens": {"claude-opus-5": {"input": 1_000_000,
                                                           "output": 1_000_000}}},
        ]
        v = works.vitalicio(regs)
        self.assertEqual(v["turnos"], 1)
        self.assertAlmostEqual(v["cost_usd"], 30.0, places=2)

    def test_soma_todos_os_registros_e_todos_os_modelos(self):
        regs = [
            {"started": 1000, "tokens": {"claude-opus-5": {"output": 1_000_000}}},
            {"started": 2000, "tokens": {"claude-opus-5": {"output": 1_000_000},
                                         "claude-haiku-4-5": {"output": 1_000_000}}},
        ]
        v = works.vitalicio(regs)
        self.assertEqual(v["turnos"], 2)
        # 25 + 25 + 5 = 55
        self.assertAlmostEqual(v["cost_usd"], 55.0, places=2)

    def test_desde_e_a_data_do_registro_mais_antigo(self):
        regs = [{"started": 1_780_000_000, "tokens": {}},
                {"started": 1_770_000_000, "tokens": {}}]
        esperado = time.strftime("%Y-%m-%d", time.localtime(1_770_000_000))
        self.assertEqual(works.vitalicio(regs)["desde"], esperado)

    def test_modelo_fora_da_tabela_nao_derruba_e_nao_inventa_custo(self):
        regs = [{"started": 1000,
                 "tokens": {"modelo-que-nao-existe": {"output": 1_000_000}}}]
        v = works.vitalicio(regs)
        self.assertEqual(v["turnos"], 1)
        self.assertEqual(v["cost_usd"], 0.0)

    def test_livro_vazio_devolve_zeros_e_desde_nulo(self):
        v = works.vitalicio([])
        self.assertEqual(v["turnos"], 0)
        self.assertEqual(v["cost_usd"], 0.0)
        self.assertIsNone(v["desde"])

    def test_registro_lixo_e_ignorado_sem_excecao(self):
        # A contabilidade nunca pode derrubar o painel.
        v = works.vitalicio([None, 42, "x", {"tokens": None},
                             {"started": 1000, "tokens": {}}])
        self.assertEqual(v["turnos"], 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
