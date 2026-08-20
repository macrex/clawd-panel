#include "view_model.h"

std::string formatAge(int s) {
    if (s <= 0)    return "agora";
    if (s < 60)    return "ha " + std::to_string(s) + "s";
    if (s < 3600)  return "ha " + std::to_string(s / 60) + "m";
    return "ha " + std::to_string(s / 3600) + "h";
}

std::string pctText(const Metric &m) {
    return m.known ? std::to_string(m.pct) + "%" : "-";
}

static int rank(Level l) {
    return l == Level::Red ? 2 : (l == Level::Yellow ? 1 : 0);
}

Level worstLevel(const Status &s) {
    int worst = 0;
    const Metric *all[] = {&s.context, &s.session, &s.week};
    for (const Metric *m : all)
        if (m->known && rank(m->level) > worst) worst = rank(m->level);
    return worst == 2 ? Level::Red : (worst == 1 ? Level::Yellow : Level::Green);
}

// Urgencia de cada estado. Bloqueado ganha de tudo: e o unico que pede acao.
static int urgencia(AgentState s) {
    switch (s) {
        case AgentState::Blocked: return 3;
        case AgentState::Working: return 2;
        case AgentState::Idle: return 1;
        default:                  return 0;
    }
}

AgentState overallState(const Status &s) {
    AgentState pior = AgentState::Unknown;
    for (const Agent &a : s.agents)
        if (urgencia(a.state) > urgencia(pior)) pior = a.state;
    return pior;
}

bool semSessao(const Status &s) {
    return !s.online && s.agents.empty() && !s.doCache;
}

int overallIndex(const Status &s) {
    const AgentState alvo = overallState(s);
    if (alvo == AgentState::Unknown) return -1;
    for (size_t i = 0; i < s.agents.size(); i++)
        if (s.agents[i].state == alvo) return (int)i;
    return -1;
}

std::string overallAgent(const Status &s) {
    const int i = overallIndex(s);
    return i < 0 ? "" : s.agents[i].repo;
}

std::string formatDuration(int seconds) {
    if (seconds < 0) seconds = 0;
    return formatApiTime((uint32_t)seconds * 1000u);
}

std::string formatTurno(int s) {
    if (s < 0) return "";
    char buf[16];
    if (s < 60)        snprintf(buf, sizeof(buf), "%ds", s);
    else if (s < 3600) snprintf(buf, sizeof(buf), "%dm%02d", s / 60, s % 60);
    else               snprintf(buf, sizeof(buf), "%dh%02d", s / 3600,
                                (s % 3600) / 60);
    return buf;
}

int turnoSegundos(int stateAgeS, uint32_t msDesdePoll, bool congelado) {
    if (stateAgeS < 0) return -1;
    if (congelado) return stateAgeS;
    return stateAgeS + (int)(msDesdePoll / 1000);
}

int turnoMonotonico(std::vector<TurnoReg> &mem, const std::string &id,
                    AgentState st, int seg) {
    const bool conta = st == AgentState::Working || st == AgentState::Blocked;

    for (size_t i = 0; i < mem.size(); i++) {
        TurnoReg &r = mem[i];
        if (r.id != id) continue;

        if (!conta) {
            // Encerrou. O que estava correndo vira o tempo DAQUELE turno e
            // fica na tela, em cinza. So o Working vira historia: o relogio do
            // Blocked conta a espera, e nao o trabalho.
            if (r.st == AgentState::Working && r.seg >= 0) r.fim = r.seg;
            r.st  = st;
            r.seg = -1;
            return r.fim;
        }
        if (seg < 0) { r.st = st; r.seg = -1; return -1; }
        if (r.st == st && r.seg >= 0 && seg < r.seg &&
            r.seg - seg <= TURNO_RECUO_TOL_S)
            return r.seg;                    // jitter: segura o ja mostrado
        r.st  = st;                          // estado novo, avanco ou recuo real
        r.seg = seg;
        return seg;
    }

    // Sem registro: so nasce com turno correndo. Uma sessao vista pela
    // primeira vez ja parada nao tem passado que a placa possa afirmar.
    if (!conta || seg < 0) return -1;
    mem.push_back({id, st, seg, -1});
    return seg;
}

void turnoPodar(std::vector<TurnoReg> &mem, const std::vector<Agent> &vivos) {
    for (size_t i = mem.size(); i > 0; i--) {
        bool vivo = false;
        for (const Agent &a : vivos)
            if (a.id == mem[i - 1].id) { vivo = true; break; }
        if (!vivo) mem.erase(mem.begin() + (i - 1));
    }
}

std::string formatApiTime(uint32_t ms) {
    const uint32_t seg = ms / 1000;
    if (seg < 60)   return std::to_string(seg) + "s";
    if (seg < 3600) return std::to_string(seg / 60) + "min";
    const uint32_t h = seg / 3600;
    const uint32_t m = (seg % 3600) / 60;
    // Minuto com dois digitos: "9h9" leria como nove horas e nove... alguma
    // coisa. "9h09" nao tem essa duvida.
    const std::string mm = (m < 10 ? "0" : "") + std::to_string(m);
    return std::to_string(h) + "h" + mm;
}

int barWidth(int pct, int fullWidth) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return fullWidth * pct / 100;
}

namespace {

// O motor de reserva vira PREFIXO do texto que ja mora ali. Rotulo proprio
// precisaria de um espaco que o rodape nao tem.
//
// E amarelo tambem por causa dele (ver quem desenha): os dois motores nao tem a
// mesma confiabilidade, e mostrar estado deduzido com a cara de estado lido
// seria a mesma mentira que o aviso de contato perdido existe para evitar.
std::string prefixoDoMotor(const Status &s) {
    return s.hooksEngine ? "HOOKS  " : "";
}

// Acima de uma hora vira horas. Nao e estetica: em segundos, um atraso de dias
// empurra este texto para a esquerda ate encostar no trio de bichos.
std::string haQuantoTempo(int staleSeconds) {
    return staleSeconds < 3600
               ? "HA " + std::to_string(staleSeconds) + "s"
               : "HA " + std::to_string(staleSeconds / 3600) + "h";
}

}   // namespace

std::string textoVetustez(const Status &s, int staleSeconds, const char *motivo,
                          bool comMasterOff) {
    const std::string pre = prefixoDoMotor(s);

    if (staleSeconds > 0)
        return pre + (motivo ? motivo : "SEM CONTATO") + " " +
               haQuantoTempo(staleSeconds);

    if (comMasterOff && s.viaSlave) {
        // Em contato, mas nao com quem manda: o dado desta tela veio da reserva,
        // e ela mesma diz como se chama.
        return pre + "MASTER OFF HA " + std::to_string(s.masterSemContatoS) +
               "s - VIA " + (s.tag.empty() ? "RESERVA" : s.tag);
    }

    return pre + formatAge(s.updated_ago);
}

std::string textoVetustezCurto(const Status &s, int staleSeconds) {
    return prefixoDoMotor(s) + "S/CONTATO " +
           (staleSeconds < 3600 ? std::to_string(staleSeconds) + "s"
                                : std::to_string(staleSeconds / 3600) + "h");
}
