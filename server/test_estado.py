#!/usr/bin/env python3
"""Testes da correção de estado: as duas arestas que não têm hook.

Os hooks são autoritativos, mas têm dois furos — não existe hook de
"desbloqueou" nem de "interrompido". Estes testes cobrem exatamente o que a API
tem permissão de deduzir sozinha, e o que ela NÃO tem.
"""

import os
import tempfile
import unittest

import claude_metrics_api as api


MARCA = ('{"type":"user","message":{"role":"user","content":'
         '[{"type":"text","text":"[Request interrupted by user]"}]}}')
ASSIST = '{"type":"assistant","message":{"role":"assistant","content":[]}}'


def transcript_com(linhas):
    fd, caminho = tempfile.mkstemp(suffix=".jsonl")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        for l in linhas:
            fh.write(l + "\n")
    return caminho


class TesteCorrigirEstado(unittest.TestCase):

    def setUp(self):
        self.lixo = []

    def tearDown(self):
        for c in self.lixo:
            try:
                os.unlink(c)
            except OSError:
                pass

    def arquivo(self, linhas):
        c = transcript_com(linhas)
        self.lixo.append(c)
        return c

    # `parado` = quanto tempo sem UMA chamada de API. Tudo depende dele.
    DEPOIS_DA_CARENCIA = 100.0 + api.GRACE_SECONDS + 1
    DEPOIS_DO_PRAZO = 100.0 + api.STALL_SECONDS + 1

    # ---- bug 1: o bloqueio que nunca acabava ----

    def test_bloqueado_com_api_andando_volta_a_working(self):
        # Chamada de API DEPOIS de o bloqueio ter sido declarado.
        rec = {"state": api.BLOCKED, "state_ts": 100.0, "api_ms_ts": 130.0,
               "data": {}}
        self.assertEqual(api.corrigir_estado(rec, 140.0), "api-ativa")

    def test_bloqueado_com_api_parada_continua_bloqueado(self):
        # A última chamada foi ANTES do bloqueio: não prova nada.
        rec = {"state": api.BLOCKED, "state_ts": 100.0, "api_ms_ts": 90.0,
               "data": {}}
        self.assertIsNone(api.corrigir_estado(rec, 140.0))

    def test_bloqueado_sem_api_ms_continua_bloqueado(self):
        # O caso que tornava o desbloqueio IMPOSSÍVEL na versão antiga: sem
        # prova não se mexe no estado. A diferença é que agora ele fica bloqueado
        # por falta de dado, e não por uma conta grossa demais para enxergar a
        # retomada.
        rec = {"state": api.BLOCKED, "state_ts": 100.0, "data": {}}
        self.assertIsNone(api.corrigir_estado(rec, 140.0))

    def test_um_ponto_de_contexto_nao_e_mais_o_criterio(self):
        # Regressão do bug: com janela de 1M, um turno curto não move o inteiro
        # do percentual. Antes isso prendia a sessão; agora o percentual não
        # participa da decisão.
        rec = {"state": api.BLOCKED, "state_ts": 100.0, "api_ms_ts": 100.1,
               "data": {"context_pct": 22}}
        self.assertEqual(api.corrigir_estado(rec, 140.0), "api-ativa")

    # ---- bug 2: o working que nunca acabava ----

    def test_working_interrompido_vira_idle(self):
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.arquivo([ASSIST, MARCA])}}
        self.assertEqual(
            api.corrigir_estado(rec, self.DEPOIS_DA_CARENCIA), "interrompido")

    def test_working_que_retomou_continua_working(self):
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.arquivo([MARCA, ASSIST])}}
        self.assertIsNone(api.corrigir_estado(rec, self.DEPOIS_DA_CARENCIA))

    def test_working_parado_alem_do_prazo_vira_idle(self):
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.arquivo([ASSIST])}}
        self.assertEqual(api.corrigir_estado(rec, self.DEPOIS_DO_PRAZO), "parado")

    def test_working_dentro_do_prazo_continua_working(self):
        # Uma ferramenta longa congela `api_ms` igual a uma interrupção. Dentro
        # do prazo, a sessão continua trabalhando — é para isso que o prazo é de
        # 10 min e não de 1.
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.arquivo([ASSIST])}}
        self.assertIsNone(api.corrigir_estado(rec, 100.0 + api.STALL_SECONDS - 1))

    def test_working_sem_transcript_ainda_tem_o_prazo(self):
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0, "data": {}}
        self.assertIsNone(api.corrigir_estado(rec, self.DEPOIS_DA_CARENCIA))
        self.assertEqual(api.corrigir_estado(rec, self.DEPOIS_DO_PRAZO), "parado")

    def test_caminho_do_transcript_pode_vir_do_hook(self):
        # Os três arquivos são instalados à mão em ~/.claude e podem sair de
        # sincronia: uma statusline antiga não manda o caminho, o hook manda.
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {},
               "transcript_path": self.arquivo([ASSIST, MARCA])}
        self.assertEqual(
            api.corrigir_estado(rec, self.DEPOIS_DA_CARENCIA), "interrompido")

    # ---- a corrida que quebrou isto AO VIVO ----

    def test_marca_recem_escrita_com_api_ativa_nao_acusa_nada(self):
        # O caso real: você interrompe e JÁ manda outra coisa. O hook
        # `UserPromptSubmit` dispara primeiro e a marca é escrita depois, então a
        # API via uma sessão recém-marcada `working` com uma marca sem resposta —
        # e a linha `assistant` que a desmentiria ainda não existia.
        #
        # A carência separa os dois: quem trabalha chama a API.
        rec = {"state": api.WORKING, "state_ts": 100.0, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.arquivo([ASSIST, MARCA])}}
        self.assertIsNone(api.corrigir_estado(rec, 100.0 + api.GRACE_SECONDS - 1))

    def test_sessao_que_dormiu_horas_nao_nasce_parada(self):
        # O silêncio é contado do INÍCIO DO TURNO, e não da última chamada de
        # API. Uma sessão parada há três horas tem `api_ms_ts` de três horas
        # atrás; medindo dali, ela seria declarada parada no mesmo instante em
        # que o prompt chegou — e o turno morreria ao nascer.
        agora = 100.0 + 3 * 3600
        rec = {"state": api.WORKING, "state_ts": agora, "api_ms_ts": 100.0,
               "data": {"transcript_path": self.arquivo([ASSIST])}}
        self.assertIsNone(api.corrigir_estado(rec, agora + 1))

    def test_deducao_e_desfeita_por_atividade_de_api(self):
        # Toda dedução desta função tem que ser reversível pela mesma prova que a
        # sustenta. Sem isto, um engano nosso durava o turno inteiro: nada mexe
        # num `idle`, e a sessão só voltava a `working` no hook seguinte.
        for quem in api.DEDUZIDOS:
            rec = {"state": api.IDLE, "event": quem, "state_ts": 100.0,
                   "api_ms_ts": 130.0, "data": {}}
            self.assertEqual(api.corrigir_estado(rec, 140.0), "api-ativa", quem)

    def test_deducao_sem_atividade_nova_continua_idle(self):
        rec = {"state": api.IDLE, "event": "interrompido", "state_ts": 100.0,
               "api_ms_ts": 90.0, "data": {}}
        self.assertIsNone(api.corrigir_estado(rec, 1e9))

    def test_idle_vindo_do_hook_Stop_nao_e_desfeito(self):
        # `Stop` é fato declarado pelo hook. Mesmo com api_ms andando (a próxima
        # sessão do mesmo processo, um subagente), não cabe a nós contradizer.
        rec = {"state": api.IDLE, "event": "Stop", "state_ts": 100.0,
               "api_ms_ts": 130.0, "data": {}}
        self.assertIsNone(api.corrigir_estado(rec, 140.0))

    # ---- o que a API NÃO tem permissão de deduzir ----

    def test_idle_do_hook_nao_e_tocado(self):
        rec = {"state": api.IDLE, "event": "Stop", "state_ts": 0.0,
               "api_ms_ts": 0.0,
               "data": {"transcript_path": self.arquivo([ASSIST, MARCA])}}
        self.assertIsNone(api.corrigir_estado(rec, 1e9))

    def test_unknown_nao_e_tocado(self):
        # Sessão sem hooks instalados. A API não sabe nada dela e não inventa.
        rec = {"state": api.UNKNOWN, "state_ts": 0.0, "api_ms_ts": 0.0,
               "data": {}}
        self.assertIsNone(api.corrigir_estado(rec, 1e9))

    def test_sem_estado_nao_e_tocado(self):
        self.assertIsNone(api.corrigir_estado({"data": {}}, 1e9))


class TesteResolveState(unittest.TestCase):
    """resolve_state virou uma leitura pura. Nada de corrigir no caminho de
    leitura, que roda a cada /status — o ESP consulta a cada 2 s."""

    def test_devolve_o_que_esta_gravado(self):
        self.assertEqual(api.resolve_state({"state": api.WORKING}, 0), api.WORKING)
        self.assertEqual(api.resolve_state({"state": api.BLOCKED}, 0), api.BLOCKED)

    def test_sem_estado_e_unknown(self):
        self.assertEqual(api.resolve_state({}, 0), api.UNKNOWN)

    def test_nao_corrige_mais_por_contexto(self):
        # Exatamente a entrada que a versão antiga transformava em WORKING.
        rec = {"state": api.BLOCKED, "state_ctx": 10, "data": {"context_pct": 90}}
        self.assertEqual(api.resolve_state(rec, 0), api.BLOCKED)


if __name__ == "__main__":
    unittest.main(verbosity=2)
