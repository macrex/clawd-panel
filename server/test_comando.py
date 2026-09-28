#!/usr/bin/env python3
"""Testes da lista branca de comandos.

Este e o primeiro ponto do sistema que AGE em vez de so contar, e a API escuta
em 0.0.0.0 sem autenticacao. A lista branca e a unica coisa entre a rede local e
o teclado dos seus agentes, entao ela merece prova propria.
"""

import io
import unittest

import claude_metrics_api as api


class TesteListaBranca(unittest.TestCase):

    def test_compact_esta_permitido(self):
        self.assertEqual(api.COMANDOS["compact"], "/compact")

    def test_clear_NAO_esta_permitido(self):
        # Fica de fora ate o modelo de confirmacao na tela estar decidido: ele
        # apaga contexto sem volta, e um toque acidental e indistinguivel de um
        # intencional.
        self.assertNotIn("clear", api.COMANDOS)

    def test_nada_de_texto_livre(self):
        # O perigo real nao e um comando errado, e um comando ARBITRARIO. Se
        # alguem trocar o dicionario por uma funcao que aceita qualquer coisa,
        # esta prova cai.
        for tentativa in ("rm -rf /", "/clear", "", None, "COMPACT",
                          "compact ", "/compact"):
            self.assertIsNone(api.COMANDOS.get(tentativa), repr(tentativa))

    def test_todo_valor_e_um_comando_de_barra(self):
        # Um valor sem a barra viraria texto digitado no prompt do agente, e nao
        # um comando — sairia como mensagem para o Claude em vez de acao.
        for nome, texto in api.COMANDOS.items():
            self.assertTrue(texto.startswith("/"), nome)
            self.assertNotIn("\n", texto, nome)   # nada de multilinha


class TesteSemPaginaDaWeb(unittest.TestCase):
    """Todo POST aceita quem o chama de verdade e recusa um site aberto no navegador."""

    # Rota -> o Content-Type que o chamador de verdade manda, sempre sem Origin.
    CHAMADORES = {
        "/responder": "application/json",                    # placa (net.cpp) e monitor
        "/command": "application/json",                      # placa
        "/enviar": "application/json",                       # monitor
        "/ingest": "application/json",                       # statusline.cjs
        "/event": "application/json",                        # claude_hook.cjs
        "/arquivo/pedir": "application/json",                # tools/atualizar_sprite.py
        "/arquivo/ok": "application/json",                   # placa
        "/arquivo/subir": "application/octet-stream",        # tools/atualizar_sprite.py
        "/tela": "application/octet-stream",                 # placa, com X-Tela
        "/tela/pedir": "application/x-www-form-urlencoded",  # tools/tela.py: urllib com corpo vazio
    }

    def test_chamador_de_verdade_passa(self):
        for rota, tipo in self.CHAMADORES.items():
            self.assertIsNone(api.avaliar_origem(rota, tipo, None), rota)
        self.assertIsNone(api.avaliar_origem("/tela/pedir", None, None))   # a placa pede a foto sem tipo
        self.assertIsNone(api.avaliar_origem("/ingest", "application/json; charset=utf-8", None))

    def test_pedido_com_origin_e_recusado_em_toda_rota(self):
        for rota, tipo in self.CHAMADORES.items():
            self.assertEqual(api.avaliar_origem(rota, tipo, "http://site.example")[0], 403, rota)
            self.assertEqual(api.avaliar_origem(rota, tipo, "null")[0], 403, rota)   # iframe sandbox, file://

    def test_post_simples_nas_rotas_de_json_e_recusado(self):
        # O que uma pagina manda sem preflight: texto, formulario, multipart.
        for rota in api.POST_JSON:
            for tipo in ("text/plain", "application/x-www-form-urlencoded",
                         "multipart/form-data; boundary=x", None, ""):
                self.assertEqual(api.avaliar_origem(rota, tipo, None)[0], 415, (rota, tipo))

    def test_do_post_passa_toda_rota_pela_trava(self):
        # A trava e pura; isto prova que o do_POST a chama, antes de ler o
        # corpo, em toda rota — inclusive numa que venha a nascer.
        for rota in list(self.CHAMADORES) + ["/rota-nova"]:
            h = api.Handler.__new__(api.Handler)   # sem __init__: sem socket
            h.path = rota
            h.headers = {"Content-Type": "text/plain", "Origin": "https://example.com"}
            enviados = []
            h._send = lambda code, payload: enviados.append(code)
            h.do_POST()
            self.assertEqual(enviados, [403], rota)


def pedir_get(rota, origem=None, host=None):
    """(status, cabecalhos) de um GET pelo do_GET e pelo _send de verdade, sem socket."""
    h = api.Handler.__new__(api.Handler)   # sem __init__: sem socket
    h.path, h.client_address = rota, ("127.0.0.1", 50000)
    h.headers = dict({"Origin": origem} if origem else {}, **({"Host": host} if host else {}))
    h.requestline, h.request_version, h.wfile = "GET %s HTTP/1.1" % rota, "HTTP/1.1", io.BytesIO()
    h.do_GET()
    cabecalhos = h.wfile.getvalue().split(b"\r\n\r\n", 1)[0].decode()
    return int(cabecalhos.split()[1]), cabecalhos


class TesteSemLeituraDaWeb(unittest.TestCase):
    """Um site aberto no navegador nao le a API: sem CORS, e os GET que mostram
    os agentes recusam `Origin`. A placa, os scripts e o monitor nao mandam."""

    def test_resposta_sem_access_control_allow_origin(self):
        for rota in ("/status", "/health", "/panes", "/rota-que-nao-existe"):
            self.assertNotIn("access-control", pedir_get(rota)[1].lower(), rota)

    def test_get_com_origin_e_recusado(self):
        for rota in ("/status", "/", "/panes", "/ler?pane_id=w0:p1", "/sessions", "/status?campos=painel"):
            self.assertEqual(pedir_get(rota, "https://example.com")[0], 403, rota)

    def test_get_sem_origin_passa(self):
        for rota in ("/status", "/", "/panes", "/sessions"):
            self.assertEqual(pedir_get(rota)[0], 200, rota)
        # Sem agente no herdr o /ler cai no 404 dele: passou da trava.
        self.assertEqual(pedir_get("/ler?pane_id=w0:p1")[0], 404)


class TesteSemDnsRebinding(unittest.TestCase):
    """Um dominio que passa a resolver para 127.0.0.1 nao le nem age na API: so
    IP e localhost passam no Host, e a placa, os scripts e o monitor usam um deles."""

    def test_ip_e_localhost_passam(self):
        for host in ("127.0.0.1:8787", "localhost:8787", "LOCALHOST", "192.168.1.24:8787", "100.64.0.7:8787",
                     "[::1]:8787", "127.0.0.1", None, ""):
            self.assertTrue(api.host_ok(host), host)

    def test_nome_e_recusado(self):
        for host in ("evil.example:8787", "evil.example", "pc.local:8787", "127.0.0.1.nip.io:8787",
                     "localhost.evil.example", "127.0.0.1:8787@evil.example"):
            self.assertFalse(api.host_ok(host), host)

    def test_get_com_nome_no_host_e_recusado(self):
        for rota in ("/status", "/panes", "/ler?pane_id=w0:p1", "/sessions", "/health"):
            self.assertEqual(pedir_get(rota, host="evil.example:8787")[0], 403, rota)
        self.assertEqual(pedir_get("/status", host="192.168.1.24:8787")[0], 200)

    def test_post_com_nome_no_host_e_recusado_antes_do_corpo(self):
        for rota in ("/ingest", "/responder", "/enviar", "/tela/pedir"):
            h = api.Handler.__new__(api.Handler)   # sem __init__: sem socket
            h.path, h.client_address = rota, ("127.0.0.1", 50000)
            h.headers = {"Content-Type": "application/json", "Host": "evil.example:8787"}
            enviados = []
            h._send = lambda code, payload: enviados.append(code)
            h.do_POST()
            self.assertEqual(enviados, [403], rota)


class TesteEnviarSoDaMaquina(unittest.TestCase):
    """O /enviar aceita texto livre: as travas dele sao a prova de que so o
    monitor desta maquina chega ao teclado dos agentes."""

    PANES = {"w0:p1"}

    def avalia(self, data, ip="127.0.0.1", cabecalho="1"):
        return api.avaliar_envio(ip, cabecalho, data, self.PANES)

    def test_texto_de_uma_linha_passa(self):
        self.assertIsNone(self.avalia({"pane_id": "w0:p1", "texto": "roda os testes de novo"}))
        self.assertIsNone(self.avalia({"pane_id": "w0:p1", "texto": "/clear"}))

    def test_escape_passa(self):
        self.assertIsNone(self.avalia({"pane_id": "w0:p1", "tecla": "Escape"}))

    def test_rede_e_recusada(self):
        # A placa mora na LAN; o texto livre, nao.
        self.assertEqual(self.avalia({"pane_id": "w0:p1", "texto": "oi"}, ip="192.168.1.50")[0], 403)

    def test_sem_cabecalho_e_recusado(self):
        # Um site no navegador consegue POST simples para 127.0.0.1, mas nao
        # cabecalho proprio sem preflight: e o cabecalho que o barra.
        self.assertEqual(self.avalia({"pane_id": "w0:p1", "texto": "oi"}, cabecalho=None)[0], 403)

    def test_pane_que_nao_e_agente_e_recusado(self):
        # Num pane de shell o texto viraria comando executado.
        self.assertEqual(self.avalia({"pane_id": "w0:p9", "texto": "rm -rf /"})[0], 404)

    def test_so_uma_linha_sem_sequencia_de_terminal(self):
        for texto in ("linha 1\nlinha 2", "sai\r", "\x1b[2J", "", "   ", "x" * (api.ENVIAR_MAX + 1), None, 42):
            self.assertEqual(self.avalia({"pane_id": "w0:p1", "texto": texto})[0], 400, repr(texto))

    def test_tecla_fora_da_lista_e_recusada(self):
        for tecla in ("C-c", "Enter", "Up", ""):
            self.assertEqual(self.avalia({"pane_id": "w0:p1", "tecla": tecla})[0], 400, tecla)


if __name__ == "__main__":
    unittest.main(verbosity=2)
