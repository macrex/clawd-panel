#!/usr/bin/env pythonw
"""
Ícone na bandeja do Windows para a API de métricas do Claude Code.

Mostra num relance o que os agentes estão fazendo, avisa quando algum fica
BLOQUEADO esperando você, e dá controle sobre a API (parar / iniciar / abrir).

    pythonw server/tray.pyw

ZERO DEPENDÊNCIAS, de propósito. A API já é stdlib pura e roda no logon; um
acessório dela não pode ser o motivo de a máquina precisar de `pip install`.
Isso custa umas 200 linhas de ctypes que uma biblioteca resolveria em 20 — o
preço é pago uma vez, aqui, e nunca mais em instalação.

SEM THREADS, também de propósito. A busca roda no WM_TIMER, na própria fila de
mensagens: contra 127.0.0.1 ela custa 2 a 14 ms medidos, então não trava nada, e
some toda a classe de bug de estado compartilhado entre thread e GUI.
"""

import ctypes
import json
import os
import struct
import subprocess
import sys
import urllib.error
import urllib.request
import webbrowser
from ctypes import wintypes

API_URL   = "http://127.0.0.1:8787"
POLL_MS   = 3000
AQUI      = os.path.dirname(os.path.abspath(__file__))
API_PY    = os.path.join(AQUI, "claude_metrics_api.py")
PID_FILE  = os.path.join(AQUI, "api.pid")

# ---------------------------------------------------------------- Win32 cru

u32, shell32, gdi32, kernel32 = (ctypes.windll.user32, ctypes.windll.shell32,
                                 ctypes.windll.gdi32, ctypes.windll.kernel32)

WM_DESTROY, WM_COMMAND, WM_TIMER = 0x0002, 0x0111, 0x0113
WM_LBUTTONUP, WM_RBUTTONUP = 0x0202, 0x0205
WM_TRAY = 0x0400 + 1                      # WM_APP+1: callback do ícone
NIM_ADD, NIM_MODIFY, NIM_DELETE = 0, 1, 2
NIF_MESSAGE, NIF_ICON, NIF_TIP, NIF_INFO = 0x01, 0x02, 0x04, 0x10
TPM_RIGHTBUTTON, TPM_RETURNCMD = 0x0002, 0x0100
MF_STRING, MF_SEPARATOR, MF_GRAYED = 0x0000, 0x0800, 0x0001

LRESULT = ctypes.c_ssize_t
WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT,
                             wintypes.WPARAM, wintypes.LPARAM)


class WNDCLASS(ctypes.Structure):
    _fields_ = [("style", wintypes.UINT), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HICON),
                ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HBRUSH),
                ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR)]


class NOTIFYICONDATA(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("hWnd", wintypes.HWND),
                ("uID", wintypes.UINT), ("uFlags", wintypes.UINT),
                ("uCallbackMessage", wintypes.UINT), ("hIcon", wintypes.HICON),
                ("szTip", wintypes.WCHAR * 128), ("dwState", wintypes.DWORD),
                ("dwStateMask", wintypes.DWORD), ("szInfo", wintypes.WCHAR * 256),
                ("uVersion", wintypes.UINT), ("szInfoTitle", wintypes.WCHAR * 64),
                ("dwInfoFlags", wintypes.DWORD)]


# Declarar as assinaturas NAO e opcional. Sem argtypes/restype, o ctypes infere
# o tipo a partir do valor Python, e qualquer handle de 64 bits estoura com
# "OverflowError: int too long to convert". Pior: onde o valor cabe por acaso,
# funciona — este arquivo rodou inteiro sob python.exe e morreu no CreateWindow
# sob pythonw.exe, so porque o modulo carregou em outro endereco.
#
# Regra: toda funcao que RECEBE ou DEVOLVE handle entra nesta lista.
LPVOID, HMENU = ctypes.c_void_p, wintypes.HANDLE

u32.DefWindowProcW.restype  = LRESULT
u32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT,
                               wintypes.WPARAM, wintypes.LPARAM]
u32.CreateWindowExW.restype  = wintypes.HWND
u32.CreateWindowExW.argtypes = [wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                wintypes.DWORD, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, ctypes.c_int, wintypes.HWND,
                                HMENU, wintypes.HINSTANCE, LPVOID]
u32.RegisterClassW.restype   = wintypes.ATOM
u32.CreatePopupMenu.restype  = HMENU
u32.AppendMenuW.argtypes     = [HMENU, wintypes.UINT, ctypes.c_size_t, wintypes.LPCWSTR]
u32.TrackPopupMenu.restype   = ctypes.c_int
u32.TrackPopupMenu.argtypes  = [HMENU, wintypes.UINT, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, wintypes.HWND, LPVOID]
u32.DestroyMenu.argtypes     = [HMENU]
u32.SetTimer.restype         = ctypes.c_size_t
u32.SetTimer.argtypes        = [wintypes.HWND, ctypes.c_size_t, wintypes.UINT, LPVOID]
u32.SetForegroundWindow.argtypes = [wintypes.HWND]
u32.DestroyWindow.argtypes   = [wintypes.HWND]
u32.GetCursorPos.argtypes    = [ctypes.POINTER(wintypes.POINT)]
u32.CreateIconFromResourceEx.restype  = wintypes.HICON
u32.CreateIconFromResourceEx.argtypes = [ctypes.c_char_p, wintypes.DWORD, wintypes.BOOL,
                                         wintypes.DWORD, ctypes.c_int, ctypes.c_int,
                                         wintypes.UINT]
kernel32.GetModuleHandleW.restype  = wintypes.HMODULE
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.OpenProcess.restype  = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
shell32.Shell_NotifyIconW.restype  = wintypes.BOOL
shell32.Shell_NotifyIconW.argtypes = [wintypes.DWORD, ctypes.c_void_p]


def make_icon(rgb, cheio=True):
    """Cria um HICON 16x16 com um círculo da cor pedida.

    Montado em memória em vez de carregado de um .ico: evita versionar quatro
    binários e mantém a cor a um passo de ser ajustada aqui no código.
    """
    W = H = 16
    r, g, b = rgb
    xor = bytearray()
    for y in range(H - 1, -1, -1):          # DIB é de baixo para cima
        for x in range(W):
            dx, dy = x - 7.5, y - 7.5
            d2 = dx * dx + dy * dy
            dentro = d2 <= 36 if cheio else 16 <= d2 <= 36
            xor += bytes((b, g, r, 255)) if dentro else b"\0\0\0\0"
    # Máscara AND toda zerada: a transparência vem do canal alfa do XOR.
    andmask = b"\0" * (4 * H)
    bih = struct.pack("<IiiHHIIiiII", 40, W, H * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    blob = bih + bytes(xor) + andmask
    return u32.CreateIconFromResourceEx(blob, len(blob), True, 0x00030000, W, H, 0)


# ------------------------------------------------------------------ estado

CORES = {                       # mesma linguagem visual do painel na placa
    "blocked": (235, 85, 85),   # vermelho: precisa de você
    "working": (217, 119, 87),  # laranja da marca: rodando agora
    "idle":    (64, 200, 120),  # verde: tudo tranquilo
    "off":     (110, 110, 110), # cinza: API fora do ar
}
ICONES = {}
ESTADO = {"status": None, "erro": "iniciando", "bloqueados_vistos": set()}
MENU_BASE = 1000                # IDs acima disto são ações fixas


def api_get(path, timeout=0.4):
    with urllib.request.urlopen(API_URL + path, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def api_pid():
    """PID da API, ou None. Confere se o processo existe antes de devolver."""
    try:
        with open(PID_FILE, encoding="utf-8") as fh:
            pid = int(fh.read().strip())
    except (OSError, ValueError):
        return None
    h = kernel32.OpenProcess(0x1000, False, pid)    # QUERY_LIMITED_INFORMATION
    if not h:
        return None
    kernel32.CloseHandle(h)
    return pid


def api_parar():
    pid = api_pid()
    if pid:
        subprocess.run(["taskkill", "/PID", str(pid), "/F"],
                       creationflags=0x08000000, check=False)


def api_iniciar():
    pythonw = os.path.join(os.path.dirname(sys.executable), "pythonw.exe")
    if not os.path.exists(pythonw):
        pythonw = sys.executable
    # DETACHED_PROCESS | CREATE_NO_WINDOW: a API sobrevive ao fechar o tray.
    subprocess.Popen([pythonw, API_PY], creationflags=0x00000008 | 0x08000000,
                     close_fds=True)


def resumo():
    """(chave_da_cor, tooltip). Uma função só, para ícone e dica nunca
    discordarem — se estiverem em lugares diferentes, um dia discordam."""
    s = ESTADO["status"]
    if not s:
        return "off", f"Claude API — {ESTADO['erro']}"

    ags = s.get("labels") or []
    bloq = [a for a in ags if a.get("state") == "blocked"]
    trab = [a for a in ags if a.get("state") == "working"]

    if bloq:   chave = "blocked"
    elif trab: chave = "working"
    else:      chave = "idle"

    linhas = [f"{len(ags)} sessao(oes)"]
    if bloq: linhas.append("BLOQUEADO: " + ", ".join(a.get("repo") or "?" for a in bloq))
    if trab: linhas.append("trabalhando: " + ", ".join(a.get("repo") or "?" for a in trab))
    if s.get("limits_fresh"):
        linhas.append(f"5h {s.get('session_pct', 0)}%  7d {s.get('week_pct', 0)}%")
    # szTip tem 128 caracteres; cortar aqui evita a dica sumir inteira.
    return chave, "\n".join(linhas)[:127]


def atualizar(hwnd, nid):
    try:
        ESTADO["status"] = api_get("/status")
        ESTADO["erro"] = ""
    except (urllib.error.URLError, OSError, ValueError, TimeoutError) as e:
        ESTADO["status"] = None
        ESTADO["erro"] = "API fora do ar" if isinstance(e, (urllib.error.URLError, OSError)) \
                         else "resposta invalida"

    chave, dica = resumo()
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE
    nid.hIcon = ICONES[chave]
    nid.szTip = dica
    shell32.Shell_NotifyIconW(NIM_MODIFY, ctypes.byref(nid))

    # Balão só na TRANSIÇÃO para bloqueado. Avisar a cada poll viraria ruído, e
    # ruído constante é como um alerta deixa de ser lido.
    s = ESTADO["status"] or {}
    agora = {a.get("session_id") for a in (s.get("labels") or [])
             if a.get("state") == "blocked"}
    novos = agora - ESTADO["bloqueados_vistos"]
    if novos:
        quais = ", ".join(a.get("repo") or "?" for a in (s.get("labels") or [])
                          if a.get("session_id") in novos)
        nid.uFlags = NIF_INFO
        nid.szInfoTitle = "Claude precisa de voce"
        nid.szInfo = f"{quais} esta bloqueado esperando resposta."
        nid.dwInfoFlags = 0x02          # ícone de aviso
        shell32.Shell_NotifyIconW(NIM_MODIFY, ctypes.byref(nid))
    ESTADO["bloqueados_vistos"] = agora


def abrir_menu(hwnd):
    s = ESTADO["status"]
    menu = u32.CreatePopupMenu()

    if s:
        ags = s.get("labels") or []
        if ags:
            for a in ags[:10]:
                marca = {"blocked": "!", "working": ">", "idle": "-"}.get(a.get("state"), "?")
                ctx = a.get("context_pct")
                txt = f"  {marca} {a.get('repo') or '?'} — {a.get('state')}"
                if ctx is not None:
                    txt += f"  ({ctx}%)"
                u32.AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_BASE + 90, txt)
        else:
            u32.AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_BASE + 90, "  nenhuma sessao")
        if s.get("limits_fresh"):
            u32.AppendMenuW(menu, MF_SEPARATOR, 0, None)
            u32.AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_BASE + 91,
                            f"  5h {s.get('session_pct', 0)}% ({s.get('session_resets_hm', '')})"
                            f"   7d {s.get('week_pct', 0)}% ({s.get('week_resets_dh', '')})")
    else:
        u32.AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_BASE + 90,
                        f"  {ESTADO['erro']}")

    u32.AppendMenuW(menu, MF_SEPARATOR, 0, None)
    u32.AppendMenuW(menu, MF_STRING, MENU_BASE + 1, "Abrir /status no navegador")
    u32.AppendMenuW(menu, MF_STRING, MENU_BASE + 2, "Abrir /sessions (detalhe)")
    u32.AppendMenuW(menu, MF_SEPARATOR, 0, None)
    if api_pid():
        u32.AppendMenuW(menu, MF_STRING, MENU_BASE + 3, "Reiniciar API")
        u32.AppendMenuW(menu, MF_STRING, MENU_BASE + 4, "Parar API")
    else:
        u32.AppendMenuW(menu, MF_STRING, MENU_BASE + 5, "Iniciar API")
    u32.AppendMenuW(menu, MF_SEPARATOR, 0, None)
    u32.AppendMenuW(menu, MF_STRING, MENU_BASE + 9, "Sair do tray")

    pt = wintypes.POINT()
    u32.GetCursorPos(ctypes.byref(pt))
    # Sem SetForegroundWindow o menu nao fecha ao clicar fora — bug classico de
    # tray no Windows, e o comportamento resultante parece travamento.
    u32.SetForegroundWindow(hwnd)
    cmd = u32.TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             pt.x, pt.y, 0, hwnd, None)
    u32.PostMessageW(hwnd, 0, 0, 0)
    u32.DestroyMenu(menu)
    return cmd


def executar(cmd, hwnd, nid):
    if   cmd == MENU_BASE + 1: webbrowser.open(API_URL + "/status")
    elif cmd == MENU_BASE + 2: webbrowser.open(API_URL + "/sessions")
    elif cmd == MENU_BASE + 3: api_parar(); api_iniciar()
    elif cmd == MENU_BASE + 4: api_parar()
    elif cmd == MENU_BASE + 5: api_iniciar()
    elif cmd == MENU_BASE + 9: u32.DestroyWindow(hwnd)


def main():
    for k, cor in CORES.items():
        ICONES[k] = make_icon(cor, cheio=(k != "off"))

    nid = NOTIFYICONDATA()
    nid.cbSize = ctypes.sizeof(NOTIFYICONDATA)
    nid.uID = 1
    nid.uCallbackMessage = WM_TRAY

    def proc(hwnd, msg, wp, lp):
        if msg == WM_TRAY:
            if lp == WM_RBUTTONUP:
                executar(abrir_menu(hwnd), hwnd, nid)
            elif lp == WM_LBUTTONUP:
                webbrowser.open(API_URL + "/status")
        elif msg == WM_TIMER:
            atualizar(hwnd, nid)
        elif msg == WM_DESTROY:
            shell32.Shell_NotifyIconW(NIM_DELETE, ctypes.byref(nid))
            u32.PostQuitMessage(0)
        return u32.DefWindowProcW(hwnd, msg, wp, lp)

    wndproc = WNDPROC(proc)     # referência viva: se coletado, o Windows chama lixo
    wc = WNDCLASS()
    wc.lpfnWndProc = wndproc
    wc.hInstance = kernel32.GetModuleHandleW(None)
    wc.lpszClassName = "ClaudeMetricsTray"
    if not u32.RegisterClassW(ctypes.byref(wc)):
        return 1

    hwnd = u32.CreateWindowExW(0, wc.lpszClassName, "Claude Metrics",
                               0, 0, 0, 0, 0, None, None, wc.hInstance, None)
    nid.hWnd = hwnd
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP
    nid.hIcon = ICONES["off"]
    nid.szTip = "Claude API — iniciando"
    shell32.Shell_NotifyIconW(NIM_ADD, ctypes.byref(nid))

    atualizar(hwnd, nid)
    u32.SetTimer(hwnd, 1, POLL_MS, None)

    msg = wintypes.MSG()
    while u32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
        u32.TranslateMessage(ctypes.byref(msg))
        u32.DispatchMessageW(ctypes.byref(msg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
