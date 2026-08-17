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
