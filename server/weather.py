#!/usr/bin/env python3
"""Data, hora e tempo — o contexto do mundo real que vai no topo do painel.

POR QUE ISTO MORA NA API, E NAO NO ESP32
A placa nao tem relogio de bateria: no boot ela nao sabe que dia e. Daria para
ela falar NTP e um serviço de clima por conta propria, mas seriam duas
conexoes externas a mais no firmware, com TLS, timeout e tratamento de erro —
e o painel ja fala com esta API a cada dois segundos. Aqui o custo e um thread
e trinta linhas; la seria mais uma fonte de travamento no laco de desenho.

SEM DEPENDENCIAS EXTERNAS, como o resto do servidor: urllib da stdlib.

FONTES
  Open-Meteo  — previsao, sem chave e sem cadastro.
  ip-api.com  — coordenadas aproximadas, UMA vez; depois ficam em weather.json.

O relogio nunca e cacheado: sai de time.localtime() a cada chamada. So o tempo
tem cache, porque e ele que custa uma requisicao.
"""

import json
import os
import threading
import time
import unicodedata
import urllib.request

AQUI = os.path.dirname(os.path.abspath(__file__))
LOCAL_FILE = os.path.join(AQUI, "weather.json")

REFRESH = 600           # 10 min: o tempo nao muda mais rapido que isso
RETRY = 60              # falhou? tenta de novo em 1 min, sem martelar
TIMEOUT = 6             # segundos por requisicao

# Dias em ASCII: a fonte embutida do Arduino_GFX so tem ASCII, entao "SÁB"
# sairia como lixo na tela. Melhor sem acento do que errado.
DIAS = ("SEG", "TER", "QUA", "QUI", "SEX", "SAB", "DOM")   # time.tm_wday: 0=segunda

# Codigos WMO -> texto curto. Curto porque o cabecalho tem ~13 caracteres de
# largura util, entao "tempestade com granizo" nao cabe de jeito nenhum.
# Sem acento, pelo mesmo motivo dos dias.
WMO = {
    0: "limpo",
    1: "quase sol", 2: "nuvens", 3: "nublado",
    45: "neblina", 48: "neblina",
    51: "garoa", 53: "garoa", 55: "garoa", 56: "garoa", 57: "garoa",
    61: "chuva", 63: "chuva", 65: "chuva forte", 66: "chuva", 67: "chuva",
    71: "neve", 73: "neve", 75: "neve", 77: "neve",
    80: "pancadas", 81: "pancadas", 82: "pancadas",
    85: "neve", 86: "neve",
    95: "trovoada", 96: "trovoada", 99: "trovoada",
}

_lock = threading.Lock()
_tempo = None       # ultimo payload de clima bom conhecido
_tempo_ts = 0.0
_local = None       # {"lat":..., "lon":..., "city":...}


def _ascii(txt):
    """Tira acento. O painel imprime com a fonte ASCII do Arduino_GFX."""
    if not txt:
        return txt
    return unicodedata.normalize("NFKD", txt).encode("ascii", "ignore").decode()


def _get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "claudio-panel/1"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return json.loads(r.read().decode("utf-8"))


def _carregar_local():
    """Coordenadas: do arquivo, ou descobertas pelo IP e gravadas nele.

    O arquivo existe para ser EDITAVEL: a geolocalizacao por IP acerta a cidade
    e erra o bairro, e quem quiser precisao troca dois numeros ali em vez de
    mexer no codigo.
    """
    global _local
    if _local:
        return _local
    try:
        with open(LOCAL_FILE, encoding="utf-8") as fh:
            d = json.load(fh)
        if isinstance(d.get("lat"), (int, float)) and isinstance(d.get("lon"), (int, float)):
            _local = {"lat": d["lat"], "lon": d["lon"], "city": _ascii(d.get("city") or "")}
            return _local
    except (OSError, ValueError, TypeError, KeyError):
        pass

    try:
        d = _get_json("http://ip-api.com/json/?fields=status,lat,lon,city")
        if d.get("status") != "success":
            return None
        _local = {"lat": d["lat"], "lon": d["lon"], "city": _ascii(d.get("city") or "")}
    except Exception:
        return None      # sem rede agora: tenta de novo no proximo ciclo

    try:
        with open(LOCAL_FILE, "w", encoding="utf-8") as fh:
            json.dump(_local, fh, indent=2)
    except OSError:
        pass             # gravar e conveniencia; a memoria ja tem o valor
    return _local


def _buscar():
    """Uma leitura do Open-Meteo. None quando nao deu."""
    loc = _carregar_local()
    if not loc:
        return None
    url = ("https://api.open-meteo.com/v1/forecast"
           f"?latitude={loc['lat']:.4f}&longitude={loc['lon']:.4f}"
           "&current=temperature_2m,weather_code,is_day"
           "&daily=temperature_2m_max,temperature_2m_min"
           "&timezone=auto&forecast_days=1")
    try:
        d = _get_json(url)
        cur = d["current"]
        dia = d.get("daily") or {}
        code = int(cur.get("weather_code") or 0)
        noite = not cur.get("is_day")
        texto = WMO.get(code, "-")
        # De noite nao ha sol para relatar. So o cabecalho ve esta diferenca, e
        # ela e o que separa "esta bom" de "esta bom e e madrugada".
        if noite and code in (0, 1):
            texto = "estrelado" if code == 0 else "poucas nuvens"
        return {
            "temp": int(round(cur["temperature_2m"])),
            "min": int(round(dia["temperature_2m_min"][0])),
            "max": int(round(dia["temperature_2m_max"][0])),
            "code": code,
            "text": texto,
            "night": bool(noite),
            "city": loc.get("city") or "",
        }
    except Exception:
        return None      # rede fora, JSON diferente, chave ausente: tudo igual


def _ciclo():
    global _tempo, _tempo_ts
    while True:
        novo = _buscar()
        if novo:
            with _lock:
                _tempo, _tempo_ts = novo, time.time()
        # Falhou? Mantem a leitura anterior — meia hora de defasagem ainda
        # informa mais do que um traco. Quem le ve a idade em `age` e decide.
        time.sleep(REFRESH if novo else RETRY)


def start():
    """Sobe o ciclo de atualizacao. Daemon: nao segura o desligamento da API."""
    threading.Thread(target=_ciclo, name="weather", daemon=True).start()


OVERRIDE_FILE = os.path.join(AQUI, "weather_override.json")


def _override(clima):
    """Costura de TESTE: um arquivo que sobrescreve campos do clima.

    Existe porque o painel decide o sprite do Clawd pela temperatura e pelos
    extremos do dia, e não dá para conferir os cinco desenhos esperando o tempo
    virar — levaria meses e um inverno. Com o arquivo presente, dá para varrer
    de mínima a máxima em segundos e ver cada faixa aparecer.

    Sobrescreve só as chaves que o arquivo traz, para poder mexer na temperatura
    sem ter que reinventar cidade, descrição e idade. Ausente ou ilegível, não
    faz nada — nunca deve derrubar o clima real.
    """
    if not clima:
        return clima
    try:
        with open(OVERRIDE_FILE, encoding="utf-8") as fh:
            clima.update(json.load(fh))
    except (OSError, ValueError):
        pass
    return clima


def snapshot():
    """Bloco pronto para entrar no /status. Nunca levanta excecao."""
    t = time.localtime()
    with _lock:
        tempo, ts = _tempo, _tempo_ts

    clima = None
    if tempo:
        clima = dict(tempo)
        clima["age"] = int(time.time() - ts)
        clima = _override(clima)

    return {
        "clock": {
            "time": time.strftime("%H:%M", t),
            "date": time.strftime("%d/%m", t),
            "weekday": DIAS[t.tm_wday],
            "epoch": int(time.time()),
        },
        # null enquanto a primeira leitura nao chega: o painel mostra um traco
        # em vez de inventar 0 grau.
        "weather": clima,
    }


if __name__ == "__main__":
    start()
    for _ in range(12):
        print(json.dumps(snapshot(), ensure_ascii=False))
        if _tempo:
            break
        time.sleep(1)
