#include <unity.h>
#include "sprite.h"
#include <cstring>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

namespace {

const uint16_t KEY = 0x18C5;   // mesma cor-chave do clawd-tank

// Monta um .clw na mao. Um sprite 2x3 assimetrico e o menor caso que ainda
// distingue as quatro rotacoes possiveis — com um quadrado simetrico, um giro
// errado passaria batido.
std::vector<uint8_t> build(uint16_t w, uint16_t h, uint16_t scale,
                           const std::vector<uint16_t> &pixels,
                           uint16_t frames = 1) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); };

    // Codifica os pixels de UM frame em corridas, e repete para os demais.
    std::vector<std::pair<uint16_t, uint16_t>> runs;
    for (uint16_t p : pixels) {
        if (!runs.empty() && runs.back().first == p) runs.back().second++;
        else runs.push_back({p, 1});
    }

    b.insert(b.end(), {'C', 'L', 'W', 'D'});
    u16(1); u16(frames); u16(w); u16(h); u16(KEY); u16(100); u16(scale); u16(0);
    for (uint16_t f = 0; f <= frames; f++) u32((uint32_t)runs.size() * f);
    for (uint16_t f = 0; f < frames; f++)
        for (auto &r : runs) { u16(r.first); u16(r.second); }
    return b;
}

// Versao para quadros DIFERENTES entre si — o caso que a ancora existe para
// resolver.
std::vector<uint8_t> buildFrames(uint16_t w, uint16_t h, uint16_t scale,
                                 const std::vector<std::vector<uint16_t>> &frames) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); };

    std::vector<std::vector<std::pair<uint16_t, uint16_t>>> runs;
    for (auto &px : frames) {
        std::vector<std::pair<uint16_t, uint16_t>> r;
        for (uint16_t p : px) {
            if (!r.empty() && r.back().first == p) r.back().second++;
            else r.push_back({p, 1});
        }
        runs.push_back(r);
    }

    b.insert(b.end(), {'C', 'L', 'W', 'D'});
    u16(1); u16((uint16_t)frames.size()); u16(w); u16(h); u16(KEY); u16(100); u16(scale); u16(0);
    uint32_t acc = 0;
    u32(0);
    for (auto &r : runs) { acc += (uint32_t)r.size(); u32(acc); }
    for (auto &r : runs) for (auto &p : r) { u16(p.first); u16(p.second); }
    return b;
}

}  // namespace

void test_blob_invalido_nao_e_aceito(void) {
    const uint8_t lixo[] = {'N', 'O', 'P', 'E', 0, 0, 0, 0};
    TEST_ASSERT_FALSE(parseSprite(lixo, sizeof(lixo)).valid);
    TEST_ASSERT_FALSE(parseSprite(nullptr, 0).valid);
}

void test_blob_truncado_nao_e_aceito(void) {
    // Cartao SD com leitura parcial nao pode virar desenho de lixo na tela.
    auto b = build(2, 3, 1, {1, 2, 3, 4, 5, 6});
    TEST_ASSERT_FALSE(parseSprite(b.data(), b.size() - 4).valid);
    TEST_ASSERT_FALSE(parseSprite(b.data(), 10).valid);
}

void test_cabecalho_e_lido(void) {
    auto b = build(2, 3, 2, {1, 2, 3, 4, 5, 6}, 4);
    Sprite s = parseSprite(b.data(), b.size());
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT16(4, s.frames);
    TEST_ASSERT_EQUAL_UINT16(2, s.width);
    TEST_ASSERT_EQUAL_UINT16(3, s.height);
    TEST_ASSERT_EQUAL_UINT16(2, s.scale);
    TEST_ASSERT_EQUAL_UINT16(KEY, s.key);
    TEST_ASSERT_EQUAL_UINT16(100, s.frameMs);
}

void test_buffer_mantem_a_orientacao_do_desenho(void) {
    // O decodificador NAO gira. A versao anterior girava, para escrever direto
    // no painel — e o painel ignora janela parcial, entao aquele caminho nunca
    // chegava na tela. Quem gira agora e o canvas.
    auto b = build(2, 3, 1, {1, 2, 3, 4, 5, 6});
    Sprite s = parseSprite(b.data(), b.size());
    TEST_ASSERT_EQUAL_INT(2, spriteBufW(s));
    TEST_ASSERT_EQUAL_INT(3, spriteBufH(s));
    TEST_ASSERT_EQUAL_UINT32(6, (uint32_t)spriteBufPixels(s));
}

void test_decodifica_na_ordem_do_desenho(void) {
    // Sai exatamente como o artista desenhou:  A B
    //                                          C D
    //                                          E F
    const uint16_t A = 0xA000, B = 0xB000, C = 0xC000,
                   D = 0xD000, E = 0xE000, F = 0xF000;
    auto b = build(2, 3, 1, {A, B, C, D, E, F});
    Sprite s = parseSprite(b.data(), b.size());

    uint16_t out[6];
    TEST_ASSERT_TRUE(decodeFrame(s, 0, out, 6, 0));
    const uint16_t esperado[6] = {A, B,
                                  C, D,
                                  E, F};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 6);
}

void test_transparente_vira_o_fundo(void) {
    const uint16_t X = 0x1234, BG = 0x0666;
    auto b = build(2, 3, 1, {X, KEY, KEY, KEY, KEY, KEY});
    Sprite s = parseSprite(b.data(), b.size());

    uint16_t out[6];
    TEST_ASSERT_TRUE(decodeFrame(s, 0, out, 6, BG));
    const uint16_t esperado[6] = { X, BG,
                                  BG, BG,
                                  BG, BG};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 6);
}

void test_bg_igual_a_chave_preserva_a_transparencia(void) {
    // E assim que o firmware chama: o buffer sai com a propria cor-chave nos
    // vazios, pronto para draw16bitRGBBitmapWithTranColor pular esses pixels.
    const uint16_t X = 0x1234;
    auto b = build(2, 2, 1, {X, KEY, KEY, X});
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[4];
    TEST_ASSERT_TRUE(decodeFrame(s, 0, out, 4, KEY));
    const uint16_t esperado[4] = {X, KEY, KEY, X};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 4);
}

void test_escala_replica_o_pixel_em_bloco(void) {
    const uint16_t A = 0xAAAA, B = 0xBBBB;
    auto b = build(1, 2, 2, {A, B});     // 1x2 nativo, escala 2 -> buffer 2x4
    Sprite s = parseSprite(b.data(), b.size());
    TEST_ASSERT_EQUAL_INT(2, spriteBufW(s));
    TEST_ASSERT_EQUAL_INT(4, spriteBufH(s));

    uint16_t out[8];
    TEST_ASSERT_TRUE(decodeFrame(s, 0, out, 8, 0));
    // A ocupa as duas primeiras linhas, B as duas ultimas — a escala vale nos
    // dois eixos.
    const uint16_t esperado[8] = {A, A,
                                  A, A,
                                  B, B,
                                  B, B};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 8);
}

void test_frames_sao_independentes(void) {
    auto b = build(2, 3, 1, {1, 2, 3, 4, 5, 6}, 3);
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t a[6], c[6];
    TEST_ASSERT_TRUE(decodeFrame(s, 0, a, 6, 0));
    TEST_ASSERT_TRUE(decodeFrame(s, 2, c, 6, 0));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(a, c, 6);   // este .clw repete o frame
}

void test_frame_fora_da_faixa_e_recusado(void) {
    auto b = build(2, 3, 1, {1, 2, 3, 4, 5, 6}, 2);
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[6];
    TEST_ASSERT_FALSE(decodeFrame(s, 2, out, 6, 0));
    TEST_ASSERT_FALSE(decodeFrame(s, -1, out, 6, 0));
}

void test_buffer_pequeno_demais_e_recusado(void) {
    // Prefere nao desenhar a estourar o buffer de quem chamou.
    auto b = build(2, 3, 1, {1, 2, 3, 4, 5, 6});
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[5];
    TEST_ASSERT_FALSE(decodeFrame(s, 0, out, 5, 0));
}

void test_corrida_maior_que_o_frame_nao_estoura(void) {
    // Arquivo corrompido no cartao: a soma das corridas passa do tamanho do
    // frame. Tem que parar na borda, nunca escrever fora.
    auto b = build(2, 3, 1, {7, 7, 7, 7, 7, 7});
    // Infla a contagem da unica corrida de 6 para 600.
    const size_t runOff = 4 + 16 + 4 * 2;      // magica + cabecalho + offsets
    b[runOff + 2] = 600 & 0xFF;
    b[runOff + 3] = 600 >> 8;

    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[6 + 4];
    std::memset(out, 0, sizeof(out));
    decodeFrame(s, 0, out, 6, 0);
    for (int i = 6; i < 10; i++)
        TEST_ASSERT_EQUAL_UINT16(0, out[i]);   // nada escrito depois do fim
}

void test_ancora_ignora_o_que_e_transitorio(void) {
    // O caso real: um quadro 4x4 onde o bicho ocupa so a linha de baixo, e um
    // balao aparece no topo em UM quadro de cinco. Centrar pelo quadro colocaria
    // a ancora no meio, com o bicho caido; a ancora do corpo ignora o balao.
    const uint16_t C = 0x2222, K = KEY;
    const std::vector<uint16_t> semBalao = {K, K, K, K,
                                            K, K, K, K,
                                            K, K, K, K,
                                            K, C, C, K};
    std::vector<uint16_t> comBalao = semBalao;
    comBalao[0] = C;                              // balao no canto superior esq.

    auto b = buildFrames(4, 4, 1, {semBalao, semBalao, semBalao, semBalao, comBalao});
    Sprite s = parseSprite(b.data(), b.size());
    TEST_ASSERT_TRUE(s.valid);

    uint8_t scratch[16];
    int cx = -1, bottom = -1;
    TEST_ASSERT_TRUE(spriteBodyAnchor(s, scratch, sizeof(scratch), cx, bottom));
    TEST_ASSERT_EQUAL_INT(2, cx);        // centro de x=1..2, e nao 2 puxado pelo balao
    TEST_ASSERT_EQUAL_INT(4, bottom);    // o corpo termina na ultima linha
}

void test_ancora_pega_o_que_e_permanente(void) {
    // O oposto: um pixel presente em TODOS os quadros conta, mesmo isolado.
    const uint16_t C = 0x3333, K = KEY;
    std::vector<uint16_t> f = {C, K, K, K,
                               K, K, K, K,
                               K, K, K, K,
                               K, K, K, K};
    auto b = buildFrames(4, 4, 1, {f, f, f});
    Sprite s = parseSprite(b.data(), b.size());
    uint8_t scratch[16];
    int cx = -1, bottom = -1;
    TEST_ASSERT_TRUE(spriteBodyAnchor(s, scratch, sizeof(scratch), cx, bottom));
    TEST_ASSERT_EQUAL_INT(0, cx);
    TEST_ASSERT_EQUAL_INT(1, bottom);
}

void test_ancora_sem_corpo_estavel_cai_no_quadro(void) {
    // Animacao em que nada se repete: nao ha corpo. Melhor devolver o centro do
    // quadro do que uma ancora inventada.
    const uint16_t C = 0x4444, K = KEY;
    std::vector<uint16_t> a = {C, K, K, K,  K, K, K, K,  K, K, K, K,  K, K, K, K};
    std::vector<uint16_t> c = {K, K, K, K,  K, K, K, K,  K, K, K, K,  K, K, K, C};
    auto b = buildFrames(4, 4, 1, {a, c});
    Sprite s = parseSprite(b.data(), b.size());
    uint8_t scratch[16];
    int cx = -1, bottom = -1;
    TEST_ASSERT_TRUE(spriteBodyAnchor(s, scratch, sizeof(scratch), cx, bottom));
    TEST_ASSERT_EQUAL_INT(2, cx);
    TEST_ASSERT_EQUAL_INT(4, bottom);
}

void test_ancora_recusa_scratch_pequeno(void) {
    auto b = build(4, 4, 1, std::vector<uint16_t>(16, KEY));
    Sprite s = parseSprite(b.data(), b.size());
    uint8_t scratch[15];
    int cx, bottom;
    TEST_ASSERT_FALSE(spriteBodyAnchor(s, scratch, sizeof(scratch), cx, bottom));
}

void test_melhor_quadro_e_o_mais_cheio(void) {
    // O quadro 0 costuma ser a pose de repouso, e em varias animacoes daqui e
    // justamente o mais vazio — escolher 0 por padrao daria o pior recorte.
    const uint16_t C = 0x5555, K = KEY;
    std::vector<uint16_t> vazio = {K, K, K, K};
    std::vector<uint16_t> meio  = {C, K, K, K};
    std::vector<uint16_t> cheio = {C, C, C, K};
    auto b = buildFrames(2, 2, 1, {vazio, meio, cheio, meio});
    Sprite s = parseSprite(b.data(), b.size());
    TEST_ASSERT_EQUAL_INT(2, spriteBestFrame(s));
}

void test_reducao_amostra_o_canto_do_bloco(void) {
    // 4x4 reduzido por 2 vira 2x2, amostrando (0,0), (2,0), (0,2), (2,2).
    const uint16_t A = 0xA000, B = 0xB000, C = 0xC000, D = 0xD000, X = 0x1111;
    auto b = build(4, 4, 1, {A, X, B, X,
                             X, X, X, X,
                             C, X, D, X,
                             X, X, X, X});
    Sprite s = parseSprite(b.data(), b.size());
    TEST_ASSERT_EQUAL_INT(2, spriteDownW(s, 2));
    TEST_ASSERT_EQUAL_INT(2, spriteDownH(s, 2));

    uint16_t out[4];
    TEST_ASSERT_TRUE(decodeFrameDown(s, 0, 2, out, 4, 0));
    const uint16_t esperado[4] = {A, B, C, D};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 4);
}

void test_reducao_respeita_a_transparencia(void) {
    const uint16_t C = 0x2222, BG = 0x0777;
    auto b = build(2, 2, 1, {KEY, C, C, C});
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[1];
    TEST_ASSERT_TRUE(decodeFrameDown(s, 0, 2, out, 1, BG));
    // A amostra e (0,0), que e transparente: fica o fundo, e nao o vizinho.
    TEST_ASSERT_EQUAL_UINT16(BG, out[0]);
}

void test_caixa_do_corpo_ignora_ilha_separada(void) {
    // O caso real do wizard: o personagem e um bloco solido de dezenas de
    // linhas, e a varinha com as faiscas e uma ilha pequena la em cima,
    // separada por um vao. As faiscas aparecem em quase todos os quadros,
    // entao a caixa dos pixels estaveis sozinha esticaria ate o topo — e o
    // icone sairia com dois tercos de vazio.
    const uint16_t C = 0x6666, K = KEY;
    std::vector<uint16_t> f = {C, K, K, K,     // ilha: 1 linha
                               K, K, K, K,     // vao
                               K, K, K, K,
                               K, C, C, K,     // corpo: 3 linhas
                               K, C, C, K,
                               K, C, C, K};
    auto b = buildFrames(4, 6, 1, {f, f, f});
    Sprite s = parseSprite(b.data(), b.size());

    uint8_t scratch[24];
    SpriteBox box;
    TEST_ASSERT_TRUE(spriteBodyBox(s, scratch, sizeof(scratch), box));
    TEST_ASSERT_EQUAL_INT(1, box.x);
    TEST_ASSERT_EQUAL_INT(3, box.y);
    TEST_ASSERT_EQUAL_INT(2, box.w);
    TEST_ASSERT_EQUAL_INT(3, box.h);
}

void test_caixa_sem_corpo_estavel_cai_no_quadro(void) {
    // Nada se repete entre os quadros: nao ha personagem a recortar. Melhor
    // devolver o quadro inteiro do que uma caixa inventada.
    const uint16_t C = 0x7777, K = KEY;
    std::vector<uint16_t> a = {C, K, K, K};
    std::vector<uint16_t> c = {K, K, K, C};
    auto b = buildFrames(2, 2, 1, {a, c});
    Sprite s = parseSprite(b.data(), b.size());

    uint8_t scratch[4];
    SpriteBox box;
    TEST_ASSERT_TRUE(spriteBodyBox(s, scratch, sizeof(scratch), box));
    TEST_ASSERT_EQUAL_INT(0, box.x);
    TEST_ASSERT_EQUAL_INT(0, box.y);
    TEST_ASSERT_EQUAL_INT(2, box.w);
    TEST_ASSERT_EQUAL_INT(2, box.h);
}

void test_caixa_recusa_scratch_pequeno(void) {
    auto b = build(4, 4, 1, std::vector<uint16_t>(16, KEY));
    Sprite s = parseSprite(b.data(), b.size());
    uint8_t scratch[15];
    SpriteBox box;
    TEST_ASSERT_FALSE(spriteBodyBox(s, scratch, sizeof(scratch), box));
}

void test_caixa_da_uniao_nao_perde_o_que_se_move(void) {
    // O caso do `confused`: a cabeca muda de lugar a cada quadro, entao nenhum
    // pixel dela e estavel e a caixa do corpo decapita o bicho. A da uniao
    // guarda tudo por onde qualquer parte passou.
    const uint16_t C = 0x8888, K = KEY;
    std::vector<uint16_t> a = {C, K, K, K,
                               K, C, K, K,
                               K, K, K, K,
                               K, K, K, K};
    std::vector<uint16_t> b2 = {K, K, K, K,
                                K, C, K, K,
                                K, K, K, C,
                                K, K, K, K};
    auto b = buildFrames(4, 4, 1, {a, b2});
    Sprite s = parseSprite(b.data(), b.size());

    SpriteBox box;
    TEST_ASSERT_TRUE(spriteUnionBox(s, box));
    TEST_ASSERT_EQUAL_INT(0, box.x);
    TEST_ASSERT_EQUAL_INT(0, box.y);
    TEST_ASSERT_EQUAL_INT(4, box.w);   // de x=0 (quadro a) a x=3 (quadro b)
    TEST_ASSERT_EQUAL_INT(3, box.h);   // de y=0 a y=2; a linha 3 fica de fora
}

void test_caixa_da_uniao_aperta_no_conteudo(void) {
    // Nao devolve o quadro inteiro por preguica: as bordas vazias saem.
    const uint16_t C = 0x9999, K = KEY;
    std::vector<uint16_t> f = {K, K, K, K,
                               K, C, C, K,
                               K, C, C, K,
                               K, K, K, K};
    auto b = buildFrames(4, 4, 1, {f, f});
    Sprite s = parseSprite(b.data(), b.size());
    SpriteBox box;
    TEST_ASSERT_TRUE(spriteUnionBox(s, box));
    TEST_ASSERT_EQUAL_INT(1, box.x);
    TEST_ASSERT_EQUAL_INT(1, box.y);
    TEST_ASSERT_EQUAL_INT(2, box.w);
    TEST_ASSERT_EQUAL_INT(2, box.h);
}

void test_caixa_da_uniao_de_animacao_vazia_cai_no_quadro(void) {
    auto b = build(3, 2, 1, std::vector<uint16_t>(6, KEY));
    Sprite s = parseSprite(b.data(), b.size());
    SpriteBox box;
    TEST_ASSERT_TRUE(spriteUnionBox(s, box));
    TEST_ASSERT_EQUAL_INT(3, box.w);
    TEST_ASSERT_EQUAL_INT(2, box.h);
}

void test_recorte_pega_so_o_que_esta_na_caixa(void) {
    const uint16_t A = 0xA000, B = 0xB000, C = 0xC000, D = 0xD000, X = 0x1111;
    auto b = build(4, 4, 1, {X, X, X, X,
                             X, A, B, X,
                             X, C, D, X,
                             X, X, X, X});
    Sprite s = parseSprite(b.data(), b.size());

    SpriteBox box;
    box.x = 1; box.y = 1; box.w = 2; box.h = 2;
    TEST_ASSERT_EQUAL_INT(2, boxDownW(box, 1));
    TEST_ASSERT_EQUAL_INT(2, boxDownH(box, 1));

    uint16_t out[4];
    TEST_ASSERT_TRUE(decodeCropDown(s, 0, box, 1, out, 4, 0));
    const uint16_t esperado[4] = {A, B, C, D};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 4);
}

void test_recorte_amostra_relativo_a_caixa(void) {
    // A grade de amostragem tem que comecar no canto da CAIXA, e nao no canto
    // do quadro: comecando no quadro, uma caixa de origem impar escolheria
    // outros pixels representantes e o icone sairia com um pixel de desvio.
    const uint16_t A = 0xA000, B = 0xB000, C = 0xC000, D = 0xD000, X = 0x1111;
    auto b = build(6, 6, 1, {X, X, X, X, X, X,
                             X, A, X, B, X, X,
                             X, X, X, X, X, X,
                             X, C, X, D, X, X,
                             X, X, X, X, X, X,
                             X, X, X, X, X, X});
    Sprite s = parseSprite(b.data(), b.size());

    SpriteBox box;
    box.x = 1; box.y = 1; box.w = 4; box.h = 4;
    uint16_t out[4];
    TEST_ASSERT_TRUE(decodeCropDown(s, 0, box, 2, out, 4, 0));
    const uint16_t esperado[4] = {A, B, C, D};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 4);
}

void test_recorte_recusa_caixa_e_buffer_invalidos(void) {
    auto b = build(4, 4, 1, std::vector<uint16_t>(16, 0x33));
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[4];

    SpriteBox ok;
    ok.x = 0; ok.y = 0; ok.w = 4; ok.h = 4;
    TEST_ASSERT_FALSE(decodeCropDown(s, 0, ok, 0, out, 4, 0));   // divisor zero
    TEST_ASSERT_FALSE(decodeCropDown(s, 0, ok, 2, out, 3, 0));   // buffer curto
    TEST_ASSERT_FALSE(decodeCropDown(s, 9, ok, 2, out, 4, 0));   // quadro invalido

    SpriteBox vazia;                                             // caixa sem area
    TEST_ASSERT_FALSE(decodeCropDown(s, 0, vazia, 1, out, 4, 0));
}

void test_reducao_recusa_divisor_e_buffer_invalidos(void) {
    auto b = build(4, 4, 1, std::vector<uint16_t>(16, 0x33));
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[4];
    TEST_ASSERT_FALSE(decodeFrameDown(s, 0, 0, out, 4, 0));   // divisor zero
    TEST_ASSERT_FALSE(decodeFrameDown(s, 0, 2, out, 3, 0));   // buffer curto
    TEST_ASSERT_FALSE(decodeFrameDown(s, 9, 2, out, 4, 0));   // quadro invalido
}

// ---- O caminho rapido: quadro inteiro, 1:1, ja opaco ----

void test_opaco_troca_a_chave_pelo_fundo(void) {
    const uint16_t A = 0xA000, B = 0xB000, F = 0x0F0F;
    auto b = build(2, 2, 1, {A, KEY, KEY, B});
    Sprite s = parseSprite(b.data(), b.size());

    uint16_t out[4];
    TEST_ASSERT_TRUE(decodeFullOpaque(s, 0, out, 4, F));
    // Nada fica transparente: o vazio SAI pintado, e e isso que dispensa
    // limpar a caixa antes do blit.
    const uint16_t esperado[4] = {A, F, F, B};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(esperado, out, 4);
}

void test_opaco_da_o_mesmo_desenho_que_o_generico(void) {
    const uint16_t A = 0xA000, B = 0xB000, C = 0xC000;
    auto b = build(3, 2, 1, {A, KEY, B,
                             KEY, C, KEY});
    Sprite s = parseSprite(b.data(), b.size());

    SpriteBox box;
    box.x = 0; box.y = 0; box.w = 3; box.h = 2;

    uint16_t rapido[6], generico[6];
    TEST_ASSERT_TRUE(decodeFullOpaque(s, 0, rapido, 6, 0x1234));
    TEST_ASSERT_TRUE(decodeCropDown(s, 0, box, 1, generico, 6, 0x1234));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(generico, rapido, 6);
}

void test_opaco_recusa_quadro_e_buffer_invalidos(void) {
    auto b = build(2, 2, 1, {1, 2, 3, 4});
    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[4];
    TEST_ASSERT_FALSE(decodeFullOpaque(s, 0, out, 3, 0));   // buffer curto
    TEST_ASSERT_FALSE(decodeFullOpaque(s, 9, out, 4, 0));   // quadro invalido
    TEST_ASSERT_FALSE(decodeFullOpaque(s, -1, out, 4, 0));
}

void test_opaco_com_corrida_maior_que_o_quadro_nao_estoura(void) {
    // O mesmo arquivo corrompido do generico: a corrida diz 600 pixels num
    // quadro de 6. Tem que parar na borda.
    auto b = build(2, 3, 1, {7, 7, 7, 7, 7, 7});
    const size_t runOff = 4 + 16 + 4 * 2;
    b[runOff + 2] = 600 & 0xFF;
    b[runOff + 3] = 600 >> 8;

    Sprite s = parseSprite(b.data(), b.size());
    uint16_t out[6 + 4];
    std::memset(out, 0, sizeof(out));
    TEST_ASSERT_TRUE(decodeFullOpaque(s, 0, out, 6, 0));
    for (int i = 6; i < 10; i++)
        TEST_ASSERT_EQUAL_UINT16(0, out[i]);
}

// ---- O relogio da animacao ----
// O CASO REAL, e nao um numero de exemplo: a volta do laco custa ~42 ms deitado
// e o `sp_cartman_descanso` pede 50 ms por quadro. Rearmando em `agora`, todo
// quadro esperava DUAS voltas (84 ms) e a fileira andava a 12 por segundo no
// lugar dos 20 do arquivo. Este teste roda um segundo de laco nas duas contas.
static int quadrosEmUmSegundo(bool acumulando, uint32_t voltaMs,
                              uint16_t frameMs) {
    uint32_t ultimo = 0;
    int quadros = 0;
    for (uint32_t agora = voltaMs; agora <= 1000; agora += voltaMs) {
        if ((agora - ultimo) < frameMs) continue;
        ultimo = acumulando ? spriteRearme(ultimo, agora, frameMs) : agora;
        quadros++;
    }
    return quadros;
}

void test_o_rearme_acumula_a_sobra_do_intervalo(void) {
    // Antes: 11 quadros por segundo. Agora: perto dos 20 que o arquivo pede.
    TEST_ASSERT_EQUAL_INT(11, quadrosEmUmSegundo(false, 42, 50));
    TEST_ASSERT_INT_WITHIN(1, 20, quadrosEmUmSegundo(true, 42, 50));

    // E nao ULTRAPASSA o ritmo do arquivo: a volta e que limita, nunca o
    // credito. Com a volta bem menor que o quadro, a conta e a mesma dos dois
    // jeitos.
    TEST_ASSERT_EQUAL_INT(20, quadrosEmUmSegundo(true, 5, 50));
    TEST_ASSERT_EQUAL_INT(20, quadrosEmUmSegundo(false, 5, 50));
}

// Parou meio segundo (tela cheia, modo terminal, leitura de cartao)? A animacao
// ressincroniza em vez de disparar uma rajada para alcancar o tempo perdido.
void test_atraso_grande_ressincroniza_em_vez_de_correr(void) {
    TEST_ASSERT_EQUAL_UINT32(1000, spriteRearme(0, 1000, 50));   // 20 quadros de atraso
    TEST_ASSERT_EQUAL_UINT32(150, spriteRearme(100, 180, 50));   // atraso de um quadro: credita
    TEST_ASSERT_EQUAL_UINT32(150, spriteRearme(100, 200, 50));   // exatamente dois: ainda credita
    TEST_ASSERT_EQUAL_UINT32(201, spriteRearme(100, 201, 50));   // passou de dois: resseta

    // frameMs zero nao pode virar laco parado nem divisao por nada.
    TEST_ASSERT_EQUAL_UINT32(77, spriteRearme(0, 77, 0));
}

// ---- O envio coalescido ----
// O CASO QUE MOTIVOU: com o teto em 100 ms e o arquivo pedindo 33 (30 fps), os
// contadores avancavam certos por baixo do pano e a tela continuava presa em
// 10 envios por segundo — o teto, e nao o arquivo nem o laco, era quem mandava.
void test_coalescer_envio_junta_avancos_dentro_do_teto(void) {
    bool pendente = false;
    uint32_t ultimoEnvio = 0;

    // Dois avancos que caem no mesmo intervalo de 33 ms viram UM envio so, no
    // instante em que o teto libera — e nao dois.
    TEST_ASSERT_FALSE(coalescerEnvio(true, 10, 33, pendente, ultimoEnvio));
    TEST_ASSERT_FALSE(coalescerEnvio(true, 20, 33, pendente, ultimoEnvio));
    TEST_ASSERT_TRUE(coalescerEnvio(false, 33, 33, pendente, ultimoEnvio));
    TEST_ASSERT_EQUAL_UINT32(33, ultimoEnvio);
    TEST_ASSERT_FALSE(pendente);
}

// Sem nenhum contador tendo avancado, nao ha o que mandar — mesmo com o
// intervalo do teto ja vencido.
void test_coalescer_envio_nao_manda_nada_sem_avanco(void) {
    bool pendente = false;
    uint32_t ultimoEnvio = 0;
    TEST_ASSERT_FALSE(coalescerEnvio(false, 1000, 33, pendente, ultimoEnvio));
    TEST_ASSERT_FALSE(pendente);
    TEST_ASSERT_EQUAL_UINT32(0, ultimoEnvio);
}

// O teto so ATRASA um envio pronto — nunca apressa um vazio. Um avanco isolado
// (sem mais nenhum antes do teto vencer) ainda sai sozinho, no instante certo.
void test_coalescer_envio_e_um_teto_e_nao_um_alvo(void) {
    bool pendente = false;
    uint32_t ultimoEnvio = 0;
    TEST_ASSERT_TRUE(coalescerEnvio(true, 500, 33, pendente, ultimoEnvio));
    TEST_ASSERT_EQUAL_UINT32(500, ultimoEnvio);

    // Novo avanco logo em seguida: espera o teto, nao manda na hora.
    TEST_ASSERT_FALSE(coalescerEnvio(true, 510, 33, pendente, ultimoEnvio));
    TEST_ASSERT_TRUE(coalescerEnvio(false, 533, 33, pendente, ultimoEnvio));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_opaco_troca_a_chave_pelo_fundo);
    RUN_TEST(test_opaco_da_o_mesmo_desenho_que_o_generico);
    RUN_TEST(test_opaco_recusa_quadro_e_buffer_invalidos);
    RUN_TEST(test_opaco_com_corrida_maior_que_o_quadro_nao_estoura);
    RUN_TEST(test_melhor_quadro_e_o_mais_cheio);
    RUN_TEST(test_reducao_amostra_o_canto_do_bloco);
    RUN_TEST(test_reducao_respeita_a_transparencia);
    RUN_TEST(test_reducao_recusa_divisor_e_buffer_invalidos);
    RUN_TEST(test_caixa_do_corpo_ignora_ilha_separada);
    RUN_TEST(test_caixa_sem_corpo_estavel_cai_no_quadro);
    RUN_TEST(test_caixa_recusa_scratch_pequeno);
    RUN_TEST(test_caixa_da_uniao_nao_perde_o_que_se_move);
    RUN_TEST(test_caixa_da_uniao_aperta_no_conteudo);
    RUN_TEST(test_caixa_da_uniao_de_animacao_vazia_cai_no_quadro);
    RUN_TEST(test_recorte_pega_so_o_que_esta_na_caixa);
    RUN_TEST(test_recorte_amostra_relativo_a_caixa);
    RUN_TEST(test_recorte_recusa_caixa_e_buffer_invalidos);
    RUN_TEST(test_ancora_ignora_o_que_e_transitorio);
    RUN_TEST(test_ancora_pega_o_que_e_permanente);
    RUN_TEST(test_ancora_sem_corpo_estavel_cai_no_quadro);
    RUN_TEST(test_ancora_recusa_scratch_pequeno);
    RUN_TEST(test_blob_invalido_nao_e_aceito);
    RUN_TEST(test_blob_truncado_nao_e_aceito);
    RUN_TEST(test_cabecalho_e_lido);
    RUN_TEST(test_buffer_mantem_a_orientacao_do_desenho);
    RUN_TEST(test_decodifica_na_ordem_do_desenho);
    RUN_TEST(test_transparente_vira_o_fundo);
    RUN_TEST(test_bg_igual_a_chave_preserva_a_transparencia);
    RUN_TEST(test_escala_replica_o_pixel_em_bloco);
    RUN_TEST(test_frames_sao_independentes);
    RUN_TEST(test_frame_fora_da_faixa_e_recusado);
    RUN_TEST(test_buffer_pequeno_demais_e_recusado);
    RUN_TEST(test_corrida_maior_que_o_frame_nao_estoura);
    RUN_TEST(test_o_rearme_acumula_a_sobra_do_intervalo);
    RUN_TEST(test_atraso_grande_ressincroniza_em_vez_de_correr);
    RUN_TEST(test_coalescer_envio_junta_avancos_dentro_do_teto);
    RUN_TEST(test_coalescer_envio_nao_manda_nada_sem_avanco);
    RUN_TEST(test_coalescer_envio_e_um_teto_e_nao_um_alvo);
    return UNITY_END();
}
