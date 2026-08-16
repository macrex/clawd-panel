#include "reset_watch.h"

const char *detectarReset(ResetWatch &w, const Status &s) {
    const char *qual = nullptr;
    if (w.visto) {
        // A API DIZ que a janela virou (`session_inferred`), e e a borda desse
        // campo que vale: ele fica ligado ate alguem chamar a API de novo — o
        // que pode levar horas —, e o aviso e do instante, nao do intervalo.
        //
        // Este e o caminho REAL, e era o que faltava. O salto de prazo abaixo
        // nunca chegava a acontecer numa virada comum: o servidor se recusa a
        // inventar o horario de uma janela que ninguem carimbou ainda, entao o
        // prazo cai a ZERO no instante da virada e so volta positivo no
        // proximo uso — e comparar contra zero e justamente o que a guarda de
        // boot (`sessaoIn > 0`) descarta.
        if (s.session.known && s.session.inferred && !w.sessaoZerada)
            qual = "SESSAO";
        // O SALTO do prazo: ele so encolhe enquanto a janela corre, e voltar a
        // crescer significa que ela virou. Continua valendo para a virada que
        // ja nasce carimbada — alguem chamou a API dentro do mesmo intervalo de
        // poll, e o `inferred` nunca chegou a ficar verdadeiro.
        else if (s.session.known && w.sessaoIn > 0 &&
                 s.session.resetsIn > w.sessaoIn + 60)
            qual = "SESSAO";
        else if (s.week.known && w.semanaIn > 0 &&
                 s.week.resetsIn > w.semanaIn + 60)
            qual = "SEMANA";
    }

    w.visto        = true;
    w.sessaoIn     = s.session.known ? s.session.resetsIn : 0;
    w.semanaIn     = s.week.known ? s.week.resetsIn : 0;
    w.sessaoZerada = s.session.known && s.session.inferred;
    return qual;
}
