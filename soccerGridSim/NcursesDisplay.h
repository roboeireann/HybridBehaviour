#pragma once

// ncurses_display.h  –  Terminal display implemented with ncurses.
//
// Unicode glyphs via ncursesw are used by default (USE_WIDE_CHARS=1 in Makefile):
//   Away team  ♟  U+265F  (red)
//   Home team  ♟  U+265F  (blue)
//   Ball       ●  U+25CF  (yellow)
// Build with USE_WIDE_CHARS=0 to fall back to ASCII '<' / '>' / 'O' with plain ncurses.
//
// Goals:    '|'  (white)
// Boundary: '='  (white)

#ifdef USE_WIDE_CHARS
#  include <ncursesw/ncurses.h>
#else
#  include <ncurses.h>
#endif

#include "Display.h"

namespace Soccer {

class NcursesDisplay : public Display {
public:
    NcursesDisplay();
    ~NcursesDisplay() override;

    void render(const GameState&)          override;
    std::optional<char> pollInput()        override;

private:
    void drawField(const GameState&);
    void drawHud(const GameState&);

    // Subwindow covering only the field + HUD rows so that werase/wrefresh
    // does not disturb the per-player trace windows below.
    WINDOW* mFieldWin{nullptr};

    // ncurses colour pair IDs
    static constexpr int PAIR_AWAY_TEAM      = 1;
    static constexpr int PAIR_HOME_TEAM      = 2;
    static constexpr int PAIR_BALL           = 3;
    static constexpr int PAIR_BORDER         = 4;
    static constexpr int PAIR_HOME_NAME_OVL = 5;
    static constexpr int PAIR_SCORE_OVL     = 6;
    static constexpr int PAIR_AWAY_NAME_OVL = 7;
};

} // namespace Soccer
