#include "eventos.h"

Eventos eventosDoPoll(const std::vector<Agent> &antes,
                      const std::vector<Agent> &agora) {
    Eventos e;
    for (const Agent &a : agora) {
        for (const Agent &b : antes) {
            if (b.id != a.id) continue;
            // Uma comemoracao POR SESSAO: com dois turnos terminando juntos a
            // turma comemora duas vezes, em sequencia. Vale tambem para o turno
            // que parou numa pergunta, como sempre valeu.
            if (b.state == AgentState::Working && a.state != AgentState::Working) {
                e.comemoracoes++;
                if (a.state != AgentState::Blocked) e.prontos++;
            }
            if (b.state != AgentState::Blocked && a.state == AgentState::Blocked)
                e.esperas++;
            break;
        }
    }
    return e;
}
