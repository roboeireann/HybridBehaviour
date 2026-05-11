#pragma once

// SimEngine.h  –  Game engine.  Owns the field, drives the sim loop,
//                 handles ball physics, agent frame mirroring for the away team,
//                 game stuck detection, and score tracking.
//
// Sim world coordinate system matches the home team's agent frame exactly:
//   Origin (0,0) at the centre of the field.
//   +x = FWD for home team (toward away goal, left-to-right on screen)
//   +y = LEFT for home team (toward the left/top side wall on screen)
//
//   x range: [-HALF_FIELD.x .. +HALF_FIELD.x]  (home goal at -HALF_FIELD.x, away goal at +HALF_FIELD.x)
//   y range: [-HALF_FIELD.y .. +HALF_FIELD.y]  (right/bottom wall at -HALF_FIELD.y, left/top wall at +HALF_FIELD.y)
//
// The internal mField array is indexed with a (+HALF_FIELD) offset via fieldCell().
// NcursesDisplay converts world coords to screen coords just before rendering.

#include "Display.h"
#include "Soccer.h"

#include <random>

namespace Soccer
{

class SimEngine
{
public:
  // Both teams must return the same size().
  // pointsToWin: first team to reach this score wins the game.
  // homeTeam attacks in +x direction; awayTeam attacks in -x direction.
  SimEngine(Team &homeTeam, Team &awayTeam, Display &display, int pointsToWin);

  // Play until one team wins, then return.
  void run();

private:
  // ── Tuning constants ─────────────────────────────────────────────────────
  static constexpr int GLOBAL_GAME_STUCK_TICKS = 100;   // ticks before ball reset
  static constexpr int INITIAL_SLOW_MICROS = 150000; // starting inter-tick delay
  static constexpr int MAX_SLOW_MICROS    = 600000;
  static constexpr int MAX_TOTAL_PLAYERS  = 8;      // hard cap: home+away combined
  static constexpr int BALL_SPEED         = 2;      // ball cells advanced per tick while in flight
  static constexpr float KICK_CONTEST_SUCCESS = 0.3f; // probability of a contested kick (in a duel) moving the ball

  // ── derived constants ────────────────────────────────────────────────────
  // HALF_FIELD: half-extents of the playable area (excluding walls/goals).
  // World x range: [-HALF_FIELD.x .. +HALF_FIELD.x]
  // World y range: [-HALF_FIELD.y .. +HALF_FIELD.y]
  static constexpr Vec2 HALF_FIELD = {FIELD_LENGTH / 2, FIELD_WIDTH / 2};

  // ── Internal field cell encoding (not exposed to agents) ─────────────────
  enum class SimCell
  {
    EMPTY,
    GOAL,
    BALL,
    WALL, // side wall - like indoor soccer
    HOME_PLAYER,  // attacks in +x direction
    AWAY_PLAYER   // attacks in -x direction
  };

  // ── Internal score encoding (not exposed to agents) ─────────────────
  enum class ScoreResult { NONE, HOME, AWAY };

  // ── Members ──────────────────────────────────────────────────────────────
  Team &mHomeTeam;    // attacks in +x direction; unmirrored frame
  Team &mAwayTeam;    // attacks in -x direction; perception rotated 180°
  Display &mDisplay;
  int mPointsToWin;
  int mHomeSize;      // mHomeTeam.numPlayers()
  int mAwaySize;      // mAwayTeam.numPlayers()
  int mTotalPlayers;  // mHomeSize + mAwaySize

  // mField is indexed [x + HALF_FIELD.x][y + HALF_FIELD.y]; use fieldCell().
  SimCell mField[FIELD_LENGTH][FIELD_WIDTH];
  // Player positions in world coords (origin at field centre).
  Vec2 mPlayers[MAX_TOTAL_PLAYERS];
  Vec2 mBallPos;
  int mHomeTeamScore, mAwayTeamScore;
  int mFrameDelayMicros;
  bool mPaused{false};    // true while simulation is paused
  bool mStepOnce{false};  // advance exactly one tick while paused

  // RNG for randomised action-resolution order each tick
  std::mt19937 mRng{std::random_device{}()};
  std::bernoulli_distribution mCoinFlip{0.5};

  // Kick state (persists across turns while ball is travelling)
  Vec2 mKickDelta;
  int mKickSteps;

  // ── Field array accessor ─────────────────────────────────────────────────
  // Converts world coords (origin at centre) to array indices.
  SimCell& fieldCell(int wx, int wy)
  {
    return mField[wx + HALF_FIELD.x][wy + HALF_FIELD.y];
  }
  const SimCell& fieldCell(int wx, int wy) const
  {
    return mField[wx + HALF_FIELD.x][wy + HALF_FIELD.y];
  }
  SimCell& fieldCell(Vec2 p)       { return fieldCell(p.x, p.y); }
  const SimCell& fieldCell(Vec2 p) const { return fieldCell(p.x, p.y); }

  // ── Per-point setup ──────────────────────────────────────────────────────
  void initField(); // clear & rebuild field; place ball
  void placeBall(); // randomise ball y, keep ball x at centre (x=0)
  void placePlayersForKickoff(bool homeHasKickoff); // place players after initField

  // ── Ball helpers ─────────────────────────────────────────────────────────
  void resetBall(); // return ball to centre (timeout penalty)

  static bool canBallEnter(SimCell c) { return c == SimCell::EMPTY || c == SimCell::GOAL; }

  // Returns true if p is within the playable world bounds (inclusive of walls/goals).
  // margin shrinks the valid region on each side.
  static bool withinField(Vec2 p, Vec2 margin={0,0})
  {
    return (p.x >= -HALF_FIELD.x + margin.x) && (p.x <= HALF_FIELD.x - margin.x)
        && (p.y >= -HALF_FIELD.y + margin.y) && (p.y <= HALF_FIELD.y - margin.y);
  }

  // ── Per-step helpers ─────────────────────────────────────────────────────
  Perception buildPerception(int cur) const;
  void applyAction(int cur, Action action);
  void updateBall();
  ScoreResult updateScore();

  Vec2 deflectedDirection(Vec2 delta);
  std::optional<Vec2> resolveDeflection(Vec2 delta, Vec2 ballPos);


  // ── Display ──────────────────────────────────────────────────────────────
  GameState buildGameState() const;

  // ── Geometry utilities (static) ──────────────────────────────────────────

  // World coords = home agent frame (same origin, same orientation).
  // Away agent frame is rotated 180° relative to world/home.
  static Vec2 agentToWorld(Vec2 agentVec, bool isHome=true)
  {
    if (isHome)
      return agentVec; // world IS the home agent frame
    else
      return {-agentVec.x, -agentVec.y}; // 180° rotation
  }

  static Vec2 worldToAgent(Vec2 worldVec, bool isHome=true)
  {
    if (isHome)
      return worldVec; // world IS the home agent frame
    else
      return {-worldVec.x, -worldVec.y}; // 180° rotation
  }

  static Vec2 actionToDelta(Action a);
  static Action deltaToAction(Vec2 delta);
  static Vec2 dirToDelta(Direction d);
  static Direction deltaToDir(Vec2 delta);
  static Direction rotateDir180(Direction d);
  static Action rotateAction180(Action a);
  static Direction computeBallDir(Vec2 playerPos, Vec2 ballPos);
};

} // namespace Soccer
