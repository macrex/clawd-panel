#include "status.h"
#include <ArduinoJson.h>
#include <cstring>
#include "jsonmem.h"

static Level levelFromColor(const char *c) {
    if (!c) return Level::Green;
    if (std::strcmp(c, "red") == 0)    return Level::Red;
    if (std::strcmp(c, "yellow") == 0) return Level::Yellow;
    return Level::Green;
}

static AgentState stateFrom(const char *s) {
    if (!s) return AgentState::Unknown;
    if (std::strcmp(s, "blocked") == 0) return AgentState::Blocked;
    if (std::strcmp(s, "working") == 0) return AgentState::Working;
    // "waiting" era o nome antigo de "idle". Aceito os dois para que uma placa
    // gravada continue funcionando contra uma API ainda nao atualizada.
    if (std::strcmp(s, "idle") == 0)    return AgentState::Idle;
    if (std::strcmp(s, "waiting") == 0) return AgentState::Idle;
    return AgentState::Unknown;
}

static std::string strOr(JsonVariantConst v, const char *fallback) {
    // Cobre ausente E null (a API manda context_branch: null).
    return v.is<const char *>() ? v.as<const char *>() : fallback;
}

// Texto de UMA linha, para qualquer coisa que va para a tela.
//
// A pergunta de um agente e copiada do terminal dele e chega com quebras de
// linha de verdade. O desenho pos o cursor onde quer e conta as linhas que
// desenhou — mas `print` trata '\n' sozinho e pula linha por conta propria,
// furando o limite e escrevendo por cima do que vem abaixo. Foi assim que uma
// pergunta de brainstorm apagou o aviso de "sem opcoes numeradas".
//
// Tabulacao e retorno viram espaco pelo mesmo motivo, e espacos repetidos
// colapsam: o que sobra e uma linha unica que a quebra por MEDIDA sabe cortar.
static std::string umaLinha(const std::string &txt) {
    std::string out;
    out.reserve(txt.size());
    bool espaco = false;
    for (char c : txt) {
        const bool branco = eControleDeCursor(c) || c == ' ';
        if (branco) {
            if (!out.empty()) espaco = true;
            continue;
        }
        if (espaco) { out += ' '; espaco = false; }
        out += c;
    }
    return out;
}

Status parseStatus(const char *json) {
    Status s;
    // O documento do poll e o maior JSON que passa por esta placa, e ele nasce
    // na PSRAM: ver jsonmem.h para o porque.
    JsonDocument doc(alocadorJson());
    if (deserializeJson(doc, json)) return s;   // s.valid continua false
    s.valid = true;

    s.online      = doc["online"] | false;
    s.sessions    = doc["sessions"] | 0;
    s.updated_ago = doc["updated_ago"] | 0;

    // Comparacao por string e nao por presenca: so `hooks` marca. Um valor
    // desconhecido de uma API futura nao pode acender o aviso sem motivo.
    const char *motor = doc["motor"] | "";
    s.hooksEngine = (std::strcmp(motor, "hooks") == 0);
    s.cleaning    = doc["cleaning"] | false;
    // Quem esta falando. Quem se identifica e o SERVICO — a placa nao guarda
    // nome de maquina nenhuma, entao instalar uma terceira nao pede cartao.
    s.tag         = strOr(doc["tag"], "");
    // O resumo dos blocos frios. Ausente numa API antiga, e ai fica vazio e o
    // reaproveitamento nao acontece — a placa recebe tudo, como sempre recebeu.
    s.frio        = strOr(doc["frio"], "");
    s.model       = strOr(doc["model"], "");
    s.repo        = strOr(doc["context_repo"], "");
    s.branch      = strOr(doc["context_branch"], "");

    // Uma flag por janela. Antes havia uma so (`limits_fresh`) e ela derrubava
    // as duas juntas: quando a janela de 5h virava, a semana — valida por mais
    // dois dias — sumia da tela junto. Uma API antiga so manda `limits_fresh`,
    // entao ele fica de reserva para os dois.
    const bool fresh = doc["limits_fresh"] | false;
    const bool sessKnown = doc["session_known"] | fresh;
    const bool weekKnown = doc["week_known"] | fresh;

    s.context.pct    = doc["context_pct"] | 0;
    s.context.level  = levelFromColor(doc["colors"]["context"]);
    s.context.known  = true;          // contexto nao depende da janela de limites

    s.session.pct      = doc["session_pct"] | 0;
    s.session.resets   = strOr(doc["session_resets_hm"], "");
    s.session.resetsIn = doc["session_resets_in"] | 0;
    s.session.at       = strOr(doc["session_resets_clock"], "");
    s.session.level    = levelFromColor(doc["colors"]["session"]);
    s.session.known    = sessKnown;
    s.session.inferred = doc["session_inferred"] | false;

    s.week.pct       = doc["week_pct"] | 0;
    s.week.resets    = strOr(doc["week_resets_dh"], "");
    s.week.resetsIn  = doc["week_resets_in"] | 0;
    s.week.at        = strOr(doc["week_resets_date"], "");
    s.week.level     = levelFromColor(doc["colors"]["week"]);
    s.week.known     = weekKnown;

    // Relogio e tempo. Ausentes numa API antiga: `known` fica false e o
    // cabecalho simplesmente nao desenha o bloco, em vez de mostrar "00:00".
    JsonVariantConst ck = doc["clock"];
    if (ck["time"].is<const char *>()) {
        s.clock.known   = true;
        s.clock.hm      = ck["time"].as<const char *>();
        s.clock.date    = strOr(ck["date"], "");
        s.clock.weekday = strOr(ck["weekday"], "");
        s.clock.epoch   = ck["epoch"] | 0L;
    }

    // O dia. Ausente numa API anterior a esta versao: `known` fica falso e a
    // linha simplesmente nao e desenhada, em vez de mostrar "0 turnos" — que
    // seria uma afirmacao sobre um dia que ninguem contou.
    JsonVariantConst wk = doc["works"];
    if (wk["trabalhos"].is<int>()) {
        s.works.known     = true;
        s.works.trabalhos = wk["trabalhos"].as<int>();
        s.works.seconds   = (int)(wk["seconds"] | 0.0f);
        s.works.blocked   = (int)(wk["blocked_seconds"] | 0.0f);
        s.works.mediana   = (int)(wk["mediana_seconds"] | 0.0f);
        s.works.hasCost   = wk["cost_usd"].is<float>();
        s.works.costUsd   = s.works.hasCost ? wk["cost_usd"].as<float>() : 0;
    }

    // A quebra por modelo do dia. Mesma regra do bloco acima: ausente deixa
    // `known` falso e o card simplesmente nao e desenhado, em vez de mostrar
    // zero — que seria uma afirmacao sobre um dia que ninguem contou.
    JsonVariantConst us = doc["uso"];
    if (us["modelos"].is<JsonArrayConst>()) {
        s.uso.known   = true;
        s.uso.costUsd = us["cost_usd"] | 0.0f;
        for (JsonVariantConst m : us["modelos"].as<JsonArrayConst>()) {
            UsoModelo um;
            um.rotulo  = strOr(m["rotulo"], "?");
            // `cost_usd` vem nulo quando o modelo nao esta na tabela de preco.
            // Nulo e diferente de zero, e o card desenha "-" nesse caso.
            um.hasCost = m["cost_usd"].is<float>();
            um.costUsd = um.hasCost ? m["cost_usd"].as<float>() : 0;
            s.uso.modelos.push_back(um);
        }
    }

    // A vida inteira. Mesma regra dos blocos acima: ausente deixa `known`
    // falso, e a quarta tela avisa o que falta em vez de mostrar nivel 1 — que
    // seria afirmar sobre uma historia que ninguem contou.
    JsonVariantConst vt = doc["vitalicio"];
    if (vt["turnos"].is<long>()) {
        s.vitalicio.known   = true;
        s.vitalicio.turnos  = vt["turnos"].as<long>();
        s.vitalicio.costUsd = vt["cost_usd"] | 0.0f;
        s.vitalicio.desde   = strOr(vt["desde"], "");
    }

    // O agente travado numa pergunta, se houver. `null` e o caso NORMAL — e por
    // isso a guarda e `pane_id`, e nao a presenca do bloco.
    //
    // `pane_id` e nao `pergunta`: sem pane nao ha para onde mandar a resposta, e
    // desenhar botoes que nao podem funcionar e pior do que nao desenhar nada.
    // A pergunta, essa, pode vir vazia de legitimo — quando a API nao conseguiu
    // ler a tela — e o bloqueio ainda tem que aparecer.
    JsonVariantConst bq = doc["bloqueio"];
    if (bq["pane_id"].is<const char *>()) {
        s.bloqueio.known    = true;
        s.bloqueio.agentId  = strOr(bq["agent_id"], "");
        s.bloqueio.paneId   = strOr(bq["pane_id"], "");
        s.bloqueio.pergunta = umaLinha(strOr(bq["pergunta"], ""));
        s.bloqueio.seq      = bq["seq"] | 0;
        s.bloqueio.tag      = s.tag;
        for (JsonVariantConst o : bq["opcoes"].as<JsonArrayConst>()) {
            if (s.bloqueio.opcoes.size() >= BLOQUEIO_MAX_OPCOES) break;
            Opcao op;
            op.n      = o["n"] | 0;
            op.rotulo = umaLinha(strOr(o["rotulo"], ""));
            op.texto  = o["texto"] | false;
            s.bloqueio.opcoes.push_back(op);
        }
    }

    // O pedido de foto da tela. Ausente na maioria esmagadora das respostas: e
    // um campo que so existe entre alguem pedir e a placa atender.
    JsonVariantConst cp = doc["captura"];
    if (cp["id"].is<long>() || cp["id"].is<int>()) {
        s.captura.known = true;
        s.captura.id    = cp["id"].as<long>();
    }

    // O pedido de arquivo novo para o cartao, tao raro quanto a captura. O nome
    // e conferido AQUI, na entrada: sem separador de caminho, o resto do
    // firmware pode concatenar "/clawd/" + nome sem pensar em travessia.
    JsonVariantConst up = doc["arquivo"];
    if ((up["id"].is<long>() || up["id"].is<int>()) &&
        up["nome"].is<const char *>() && up["bytes"].is<long>()) {
        const std::string nome = up["nome"].as<const char *>();
        const bool limpo = !nome.empty() && nome.size() < 64 &&
                           nome.find('/') == std::string::npos &&
                           nome.find('\\') == std::string::npos &&
                           nome.find("..") == std::string::npos;
        if (limpo) {
            s.atualizacao.known     = true;
            s.atualizacao.id        = up["id"].as<long>();
            s.atualizacao.nome      = nome;
            s.atualizacao.bytes     = up["bytes"].as<long>();
            s.atualizacao.crc       = up["crc"].as<uint32_t>();
            s.atualizacao.reiniciar = up["reiniciar"] | true;
        }
    }

    // null enquanto a API nao conseguiu a primeira leitura — e null e diferente
    // de zero grau.
    JsonVariantConst wt = doc["weather"];
    if (wt["temp"].is<int>()) {
        s.weather.known = true;
        s.weather.temp  = wt["temp"].as<int>();
        s.weather.tmin  = wt["min"] | 0;
        s.weather.tmax  = wt["max"] | 0;
        s.weather.text  = strOr(wt["text"], "");
        s.weather.city  = strOr(wt["city"], "");
        s.weather.age   = wt["age"] | 0;
    }

    // labels[] = um agente por entrada, ja ordenado pela API.
    for (JsonVariantConst v : doc["labels"].as<JsonArrayConst>()) {
        Agent a;
        a.id     = strOr(v["session_id"], "");
        a.repo   = strOr(v["repo"], "");
        a.branch = strOr(v["branch"], "");
        a.agent  = strOr(v["agent"], "");
        a.model  = strOr(v["model"], "");
        a.effort = strOr(v["effort"], "");
        a.level  = levelFromColor(v["color"]);
        a.age    = v["age"] | 0;
        a.stale  = v["stale"] | (v["idle"] | false);   // ausente = publicando
        // Campo ausente (API antiga) cai em Unknown, e Unknown nao pulsa: uma
        // versao velha do servidor deixa o painel discreto, nunca alarmado.
        a.state  = stateFrom(v["state"]);
        a.done   = v["done"] | false;
        // context_pct null significa "nao reportado", nao zero.
        a.hasContext  = v["context_pct"].is<int>();
        a.contextPct  = a.hasContext ? v["context_pct"].as<int>() : 0;

        // Cumulativos da sessao. `is<float>()` aceita inteiro tambem no
        // ArduinoJson, entao um custo redondo (0, 12) nao se perde.
        a.hasCost      = v["cost_usd"].is<float>();
        a.costUsd      = a.hasCost ? v["cost_usd"].as<float>() : 0;
        // As duas linhas andam juntas: a API publica ambas ou nenhuma, e meia
        // resposta ("+37 linhas, removidas desconhecidas") nao informa nada.
        a.hasLines     = v["lines_added"].is<int>() && v["lines_removed"].is<int>();
        a.linesAdded   = a.hasLines ? v["lines_added"].as<int>() : 0;
        a.linesRemoved = a.hasLines ? v["lines_removed"].as<int>() : 0;
        a.hasApiMs     = v["api_ms"].is<uint32_t>();
        a.apiMs        = a.hasApiMs ? v["api_ms"].as<uint32_t>() : 0;
        // Vazio quando a API nao manda (versao anterior) ou quando o herdr nao
        // enxerga esta sessao. Nos dois casos o painel esconde o botao de
        // terminal em vez de oferecer um que nao teria para onde apontar.
        a.paneId       = strOr(v["pane_id"], "");
        // A tag e do PAYLOAD, nao do agente: todo mundo que veio nesta resposta
        // roda na maquina que respondeu.
        a.tag          = s.tag;

        s.agents.push_back(a);
    }

    return s;
}
