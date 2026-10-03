#include "sudoku_app.h"

#include <cstdio>
#include <new>
#include <lvgl.h>
#include "games/registry.h"
#include "sudoku_screen.h"
#include "sudoku_stock.h"
#include "ui/shell.h"
#include "ui/theme.h"

namespace sudoku_app {

namespace {

// Allocated while Sudoku is open, freed when the player leaves it.
sudoku::Game* game = nullptr;           // ~4 KB (undo history)
uint8_t*      buf  = nullptr;           // save image
size_t        buf_cap = 0;

void free_all()
{
    delete game;
    delete[] buf;
    game = nullptr;
    buf = nullptr;
    buf_cap = 0;
}

bool alloc_all()
{
    buf_cap = sudoku::Game::max_serialized_size();
    game = new (std::nothrow) sudoku::Game();
    buf  = new (std::nothrow) uint8_t[buf_cap];
    if (game && buf) return true;
    free_all();
    return false;
}

bool load_into(sudoku::Game& g, uint8_t* b, size_t cap)
{
    const ui::Shell& H = ui::shell();
    if (!H.load_game) return false;
    const size_t n = H.load_game(kId, b, cap);
    return n && g.deserialize(b, n) && g.active();
}

// ---- Registry entry --------------------------------------------------------
void open()
{
    if (!alloc_all()) {                 // out of memory: stay on the picker
        ui::app_go_home();
        return;
    }
    if (!load_into(*game, buf, buf_cap)) {
        sudoku::Rng rng(ui::shell().random_seed ? ui::shell().random_seed() : 1);
        game->start(sudoku::Difficulty::Easy, rng);
        save(*game);
    }
    sudoku_stock_begin();
    sudoku_ui::screen_create(*game);
}

void close()
{
    if (!game) return;
    save(*game);
    sudoku_ui::screen_destroy();
    sudoku_stock_end();
    free_all();
}

void save_now()
{
    if (game) save(*game);
}

void tick(uint32_t now_ms)
{
    sudoku_ui::screen_tick(now_ms);
    sudoku_stock_loop();
}

void restyle() { sudoku_ui::screen_restyle(); }

void describe(const sudoku::Game& g, char* out, size_t cap)
{
    char t[16];
    sudoku::stats::format_time(t, sizeof t, g.elapsed_s());
    const char* d = sudoku::difficulty_name(g.difficulty());
    if (g.solved()) snprintf(out, cap, "%s solved, %s", d, t);
    else            snprintf(out, cap, "%s, %s", d, t);
}

bool summary(char* out, size_t cap)
{
    if (game) { describe(*game, out, cap); return true; }
    // Not open: read the save into a temporary game
    sudoku::Game* g = new (std::nothrow) sudoku::Game();
    const size_t n = sudoku::Game::max_serialized_size();
    uint8_t* b = new (std::nothrow) uint8_t[n];
    const bool ok = g && b && load_into(*g, b, n);
    if (ok) describe(*g, out, cap);
    delete g;
    delete[] b;
    return ok;
}

// Icon: one 3x3 box of a Sudoku with the middle cell selected.
void icon_draw_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const ui::Palette& P = ui::pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int size = lv_area_get_width(&a);
    const int border = size >= 48 ? 3 : 2;
    const int cell = (size - 2 * border - 2) / 3;
    const int inner = 3 * cell + 2;
    const int ox = a.x1 + (size - inner - 2 * border) / 2, oy = a.y1 + (size - inner - 2 * border) / 2;

    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_COVER;
    r.radius = 0;
    auto fill = [&](int x1, int y1, int x2, int y2, lv_color_t c) {
        r.bg_color = c;
        lv_area_t ar{x1, y1, x2, y2};
        lv_draw_rect(layer, &r, &ar);
    };
    const int total = inner + 2 * border;
    fill(ox, oy, ox + total - 1, oy + total - 1, P.line_thick);           // outer frame
    fill(ox + border, oy + border, ox + border + inner - 1, oy + border + inner - 1, P.line_thin);

    static const char digits[9] = {'5', 0, '9', 0, '7', 0, '2', 0, '4'};
    const lv_font_t* f = cell >= 24 ? &lv_font_montserrat_20
                       : cell >= 17 ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = f;
    ld.align = LV_TEXT_ALIGN_CENTER;
    const int lh = lv_font_get_line_height(f);
    for (int i = 0; i < 9; ++i) {
        const int x = ox + border + (i % 3) * (cell + 1), y = oy + border + (i / 3) * (cell + 1);
        fill(x, y, x + cell - 1, y + cell - 1, i == 4 ? P.selected : P.cell);
        if (!digits[i]) continue;
        char s[2] = {digits[i], 0};
        ld.text = s;
        ld.text_local = 1;
        ld.color = i == 4 ? P.entry : P.given;
        lv_area_t ta{x, y + (cell - lh) / 2, x + cell - 1, y + (cell - lh) / 2 + lh - 1};
        lv_draw_label(layer, &ld, &ta);
    }
}

void icon(lv_obj_t* parent, int size)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_center(o);
    lv_obj_set_clickable(o, false);
    lv_obj_add_event_cb(o, icon_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
}

void stats_line_cb(const char* line, void* ctx)
{
    sudoku::stats::Record r;
    if (sudoku::stats::parse_line(line, r)) static_cast<sudoku::stats::Summary*>(ctx)->add(r);
}

} // namespace

void save(const sudoku::Game& g)
{
    const ui::Shell& H = ui::shell();
    if (!H.save_game || !buf) return;
    const size_t n = g.serialize(buf, buf_cap);
    if (n) H.save_game(kId, buf, n);
}

void record_stat(const sudoku::stats::Record& r)
{
    const ui::Shell& H = ui::shell();
    char body[96];
    if (H.stats_append && sudoku::stats::format_body(body, sizeof body, r))
        H.stats_append(kId, sudoku::stats::kCsvHeader, body);
}

bool load_stats(sudoku::stats::Summary& out)
{
    out = sudoku::stats::Summary{};
    const ui::Shell& H = ui::shell();
    return H.stats_read && H.stats_read(kId, stats_line_cb, &out);
}

} // namespace sudoku_app

namespace games {
extern const GameOps sudoku_ops;
const GameOps sudoku_ops = {
    sudoku_app::open, sudoku_app::close, sudoku_app::save_now, sudoku_app::tick,
    sudoku_app::restyle, sudoku_app::summary, sudoku_app::icon,
};
} // namespace games
