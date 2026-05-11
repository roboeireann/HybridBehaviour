#pragma once

// display.h  –  Abstract display interface and the GameState snapshot
//               that the engine passes to it every turn.

#include "Soccer.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Soccer
{

// Snapshot of visible world state, built by the engine and handed to
// Display::render() each turn.
struct GameState
{
  int fieldWidth, fieldHeight;
  Vec2 ballPos;
  std::vector<Vec2> homePlayers; // real-world coordinates
  std::vector<Vec2> awayPlayers;
  int homeScore, awayScore;
  std::string homeName, awayName;
  int frameDelayMicros; // current inter-step delay in microseconds (for HUD)
  bool paused{false}; // true while simulation is paused (for HUD indicator)
};

// ──────────────────────────── Display interface ───────────────────────────────
class Display
{
public:
  // Draw the current game state.  Called once per player turn.
  virtual void render(const GameState &) = 0;

  // Non-blocking key poll.  Returns the pressed character, or nullopt if
  // no input is waiting.  Recognised keys: 'q' quit, 's' slower, 'f' faster.
  virtual std::optional<char> pollInput() = 0;

  virtual ~Display() = default;
};

// ──────────────────────────── Null display ───────────────────────────────────
// Used in headless / benchmark mode.  render() is a no-op, pollInput()
// always returns nullopt.
class NullDisplay : public Display
{
public:
  void render(const GameState &) override {}
  std::optional<char> pollInput() override { return std::nullopt; }
};

} // namespace Soccer
