// ncurses_display.cpp
//
// Unicode glyphs via ncursesw are used by default.
// Build with USE_WIDE_CHARS=0 to fall back to ASCII with plain ncurses.
// The ncurses header is pulled in transitively via NcursesDisplay.h so that
// the same USE_WIDE_CHARS guard controls which variant is included.

#include "NcursesDisplay.h"

#include <clocale> // setlocale – required for wide-char ncurses
#include <cstdio>
#include <cstring> // strlen

namespace Soccer
{

NcursesDisplay::NcursesDisplay()
{
#ifdef USE_WIDE_CHARS
  // Wide-char ncurses requires a UTF-8 locale to be set before initscr().
  setlocale(LC_ALL, "");
#endif
  initscr();
  cbreak();
  noecho();
  nodelay(stdscr, TRUE); // make getch() non-blocking
  keypad(stdscr, TRUE);
  curs_set(0); // hide cursor
  flushinp();  // discard any input that arrived before ncurses took over

  if (has_colors())
  {
    start_color();
    // init_pair(PAIR_AWAY_TEAM, COLOR_RED, COLOR_BLACK);
    // init_pair(PAIR_HOME_TEAM, COLOR_BLUE, COLOR_BLACK);
    init_pair(PAIR_AWAY_TEAM, COLOR_WHITE, COLOR_RED);
    init_pair(PAIR_HOME_TEAM, COLOR_WHITE, COLOR_BLUE);
    init_pair(PAIR_BALL, COLOR_YELLOW, COLOR_BLACK);
    init_pair(PAIR_BORDER, COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_HOME_NAME_OVL, COLOR_WHITE, COLOR_BLUE); // reversed
    init_pair(PAIR_SCORE_OVL, COLOR_BLACK, COLOR_WHITE);    // reversed
    init_pair(PAIR_AWAY_NAME_OVL, COLOR_WHITE, COLOR_RED);  // reversed
  }
}

NcursesDisplay::~NcursesDisplay() { endwin(); }

void NcursesDisplay::render(const GameState &gs)
{
  // ── Lazy creation of the field subwindow ─────────────────────────────
  // Covers field rows + the HUD line only, so that werase/wrefresh here
  // does not disturb per-player trace subwindows drawn below.
  // fieldWidth/fieldHeight are FIELD_LENGTH/FIELD_WIDTH (screen cols/rows).
  if (!mFieldWin)
    mFieldWin = subwin(stdscr, gs.fieldHeight + 1, gs.fieldWidth, 0, 0);

  if (!mFieldWin)
  {
    // Fallback: full-screen erase (no trace windows possible).
    erase();
    drawField(gs);
    drawHud(gs);
    refresh();
  }
  else
  {
    werase(mFieldWin);
    drawField(gs);
    drawHud(gs);
    wrefresh(mFieldWin);
  }
}

void NcursesDisplay::drawField(const GameState &gs)
{
  WINDOW *w = mFieldWin ? mFieldWin : stdscr;
  bool colours = has_colors();

  // World coords have origin at field centre: +x = right (FWD for home),
  // +y = LEFT = screen top.  Convert to screen coords:
  //   screenCol = worldX + halfW    (halfW = fieldWidth  / 2)
  //   screenRow = halfH - worldY    (halfH = fieldHeight / 2; flip y)
  const int halfW = gs.fieldWidth  / 2;
  const int halfH = gs.fieldHeight / 2;
  auto toScreenCol = [&](int wx) { return wx + halfW; };
  auto toScreenRow = [&](int wy) { return halfH - wy; };

  // ── Boundaries (top/bottom screen rows = y = ±halfH walls) ──────────
  if (colours)
    wattron(w, COLOR_PAIR(PAIR_BORDER));
  for (int sc = 0; sc < gs.fieldWidth; ++sc)
  {
    mvwaddch(w, 0,                  sc, '='); // left/top wall   (worldY = +halfH)
    mvwaddch(w, gs.fieldHeight - 1, sc, '='); // right/bot wall  (worldY = -halfH)
  }
  // ── Goal columns (leftmost/rightmost screen cols = x = ±halfW) ───────
  for (int sr = 1; sr < gs.fieldHeight - 1; ++sr)
  {
    mvwaddch(w, sr, 0,                 '|'); // home goal  (worldX = -halfW)
    mvwaddch(w, sr, gs.fieldWidth - 1, '|'); // away goal  (worldX = +halfW)
  }
  if (colours)
    wattroff(w, COLOR_PAIR(PAIR_BORDER));

  // ── Away players (attack in -x direction, right-to-left on screen) ───
  if (colours)
    wattron(w, COLOR_PAIR(PAIR_AWAY_TEAM));
#ifdef USE_WIDE_CHARS
  {
    auto getGlyph = [](int n) { return static_cast<wchar_t>(0x2460 + (n - 1)); };

    cchar_t wch;
    // static const wchar_t away_glyph[] = {L'\u265F', L'\0'};
    // setcchar(&wch, away_glyph, A_NORMAL, PAIR_AWAY_TEAM, nullptr);
    // for (auto pos : gs.awayPlayers)
    //   mvwadd_wch(w, toScreenRow(pos.y), toScreenCol(pos.x), &wch);
    int i=1;
    for (auto pos : gs.awayPlayers)
    {
      wchar_t away_glyph[] = {getGlyph(i++), L'\0'};
      setcchar(&wch, away_glyph, A_NORMAL, PAIR_AWAY_TEAM, nullptr);
      mvwadd_wch(w, toScreenRow(pos.y), toScreenCol(pos.x), &wch);
    }
  }
#else
  for (auto pos : gs.awayPlayers)
    mvwaddch(w, toScreenRow(pos.y), toScreenCol(pos.x), '<');
#endif
  if (colours)
    wattroff(w, COLOR_PAIR(PAIR_AWAY_TEAM));

  // ── Home players (attack in +x direction, left-to-right on screen) ───
  if (colours)
    wattron(w, COLOR_PAIR(PAIR_HOME_TEAM));
#ifdef USE_WIDE_CHARS
  {
    auto getGlyph = [](int n) { return static_cast<wchar_t>(0x2460 + (n - 1)); };

    cchar_t wch;
    // static const wchar_t home_glyph[] = {L'\u265F', L'\0'};
    // setcchar(&wch, home_glyph, A_NORMAL, PAIR_HOME_TEAM, nullptr);
    // for (auto pos : gs.homePlayers)
    //   mvwadd_wch(w, toScreenRow(pos.y), toScreenCol(pos.x), &wch);
    int i=1;
    for (auto pos : gs.homePlayers)
    {
      wchar_t home_glyph[] = {getGlyph(i++), L'\0'};
      setcchar(&wch, home_glyph, A_NORMAL, PAIR_HOME_TEAM, nullptr);
      mvwadd_wch(w, toScreenRow(pos.y), toScreenCol(pos.x), &wch);
    }

  }
#else
  for (auto pos : gs.homePlayers)
    mvwaddch(w, toScreenRow(pos.y), toScreenCol(pos.x), '>');
#endif
  if (colours)
    wattroff(w, COLOR_PAIR(PAIR_HOME_TEAM));

  // ── Ball ● / 'O' ─────────────────────────────────────────────────────
  if (colours)
    wattron(w, COLOR_PAIR(PAIR_BALL));
#ifdef USE_WIDE_CHARS
  {
    cchar_t wch;
    // static const wchar_t ball_glyph[] = {L'\u25CF', L'\0'};
    static const wchar_t ball_glyph[] = {L'\u2022', L'\0'};
    setcchar(&wch, ball_glyph, A_NORMAL, PAIR_BALL, nullptr);
    mvwadd_wch(w, toScreenRow(gs.ballPos.y), toScreenCol(gs.ballPos.x), &wch);
  }
#else
  mvwaddch(w, toScreenRow(gs.ballPos.y), toScreenCol(gs.ballPos.x), 'O');
#endif
  if (colours)
    wattroff(w, COLOR_PAIR(PAIR_BALL));
}

void NcursesDisplay::drawHud(const GameState &gs)
{
  WINDOW *w = mFieldWin ? mFieldWin : stdscr;
  bool colours = has_colors();

  // ── Score overlay: top-left of the field ─────────────
  {
    // --- Home name (left) ---
    int col = 2;
    if (colours)
      wattron(w, COLOR_PAIR(PAIR_HOME_NAME_OVL));
    mvwprintw(w, 0, col, " %-15s ", gs.homeName.c_str());
    if (colours)
      wattroff(w, COLOR_PAIR(PAIR_HOME_NAME_OVL));
    col += 17;

    // score mid
    if (colours)
      wattron(w, COLOR_PAIR(PAIR_SCORE_OVL) | A_BOLD);
    mvwprintw(w, 0, col, " %2d - %-2d ", gs.homeScore, gs.awayScore);
    if (colours)
      wattroff(w, COLOR_PAIR(PAIR_SCORE_OVL) | A_BOLD);
    col += 9;

    // away team right
    if (colours)
      wattron(w, COLOR_PAIR(PAIR_AWAY_NAME_OVL));
    mvwprintw(w, 0, col, " %15s ", gs.awayName.c_str());
    if (colours)
      wattroff(w, COLOR_PAIR(PAIR_AWAY_NAME_OVL));
  }

  // ── Key-menu bar: one row below the field ────────────────────────────
  char menuBuf[120];
  std::snprintf(menuBuf, sizeof(menuBuf), " [q]uit  [s]lower  [f]aster  [space]pause  [n]step%s",
                gs.paused ? "   ** PAUSED **" : "");
  mvwaddstr(w, gs.fieldHeight, 0, menuBuf);
}

std::optional<char> NcursesDisplay::pollInput()
{
  int ch = getch();
  if (ch == ERR)
    return std::nullopt;
  return static_cast<char>(ch);
}

} // namespace Soccer
