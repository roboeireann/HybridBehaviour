#include "HbsExampleTeamPlayers.h"


#ifdef USE_WIDE_CHARS
#include <ncursesw/ncurses.h>
#else
#include <ncurses.h>
#endif

#include <format>
#include <iostream>

#define TLOG(...) std::cerr << std::format(__VA_ARGS__) << '\n'

// ── Trace window layout constants ────────────────────────────────────────────
// The trace windows sit below the field and the key-menu bar.
//   Row 0 .. FIELD_HEIGHT-1  : field
//   Row FIELD_HEIGHT          : key-menu bar
//   Row FIELD_HEIGHT+1 ..    : per-player trace windows
static constexpr int TRACE_START_ROW = Soccer::FIELD_WIDTH + 1;
static constexpr int TRACE_WIN_ROWS = 20; // max rows per trace window

void HbsExamplePlayer::displayTaskTrace()
{
  TLOG("displayTaskTrace");

  const int playerNum = playerIndex+1;

  // ── Lazy subwindow creation ───────────────────────────────────────────────
  // Each player owns a fixed subwindow positioned by playerIndex.
  // We create it once; if the terminal is too small we skip gracefully.
  if (!mTraceWindow)
  {
    int termRows, termCols;
    getmaxyx(stdscr, termRows, termCols);

    // Reserve 1 column as a separator between adjacent player windows.
    static constexpr int SEP = 1;
    int slotWidth   = (numPlayers > 0) ? (termCols / numPlayers) : termCols;
    int winWidth    = slotWidth - SEP;          // actual window width (excludes separator)
    int winStartCol = playerIndex * slotWidth;  // window starts after previous slot
    int winRows     = std::min(TRACE_WIN_ROWS, termRows - TRACE_START_ROW);

    TLOG("P{}: termRows={} termCols={} winRows={} winWidth={} startCol={} TRACE_START_ROW={}",
         playerNum, termRows, termCols, winRows, winWidth, winStartCol, TRACE_START_ROW);

    if (winRows <= 0 || winWidth <= 0)
    {
      TLOG("P{}: window too small, skipping", playerNum);
      return;
    }

    mTraceWindow = static_cast<void *>(subwin(stdscr, winRows, winWidth, TRACE_START_ROW, winStartCol));

    TLOG("P{}: subwin result={}", playerNum, mTraceWindow != nullptr ? "OK" : "NULL");

    if (!mTraceWindow)
      return;
  }

  WINDOW *win = static_cast<WINDOW *>(mTraceWindow);
  int winRows, winWidth;
  getmaxyx(win, winRows, winWidth);

  wclear(win);

  // ── Header row: "Player N" reversed ──────────────────────────────────────
  wattron(win, A_REVERSE);
  mvwhline(win, 0, 0, ' ', winWidth);
  mvwprintw(win, 0, 0, " Player %d ", playerNum);
  wattroff(win, A_REVERSE);

  // ── Trace frames ─────────────────────────────────────────────────────────
  const auto &frames = tickTrace.getFrames();
  int row = 1;

  for (const auto &frame : frames)
  {
    if (row + 1 >= winRows)
      break;

    // Line 1: task name (left, indented by depth) + task duration (right)
    mvwprintw(win, row, frame.depth, "%s", frame.taskName);
    mvwprintw(win, row, winWidth - 13, "%4d / %6d", frame.taskTicks, frame.taskDuration);
    ++row;

    // Line 2: state name (left, indented by depth+2) + state duration (right)
    mvwprintw(win, row, frame.depth + 2, "state=%s", frame.stateName);
    mvwprintw(win, row, winWidth - 13, "%4d / %6d", frame.stateTicks, frame.stateDuration);
    ++row;

    // Per-tick annotation strings, indented one extra level.
    for (const auto &s : frame.strings)
    {
      if (row >= winRows)
        break;
      mvwprintw(win, row, frame.depth + 2, "%s", s.c_str());
      ++row;
    }
  }

  wrefresh(win);
}
