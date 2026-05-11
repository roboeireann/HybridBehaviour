/**
 * SimEngine.cpp
 *
 * Sim world coordinate system has its origin at the centre of the field
 * and matches the home team's agent frame exactly:
 *
 *   +x = FWD for home team (toward away goal, left-to-right on screen)
 *   +y = LEFT for home team (toward the left/top side wall on screen)
 *
 *   x range: [-HALF_FIELD.x .. +HALF_FIELD.x]
 *             home goal line at x = -HALF_FIELD.x
 *             away goal line at x = +HALF_FIELD.x
 *
 *   y range: [-HALF_FIELD.y .. +HALF_FIELD.y]
 *             right/bottom wall at y = -HALF_FIELD.y
 *             left/top wall    at y = +HALF_FIELD.y
 *
 * The away team's agent frame is the world frame rotated 180° (both axes negated).
 *
 * The internal mField array is indexed via fieldCell(x, y) which applies the
 * (+HALF_FIELD) offset to convert world coords to array indices.
 *
 * NcursesDisplay converts world coords to screen coords just before rendering:
 *   screenCol = worldX + HALF_FIELD.x
 *   screenRow = HALF_FIELD.y - worldY   (y flipped: +y=LEFT = screen top)
 */

#include "SimEngine.h"

#include <algorithm>  // std::shuffle
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>    // std::iota
#include <unistd.h>   // usleep
#include <vector>

namespace Soccer
{

// ─────────────────────────── Constructor ─────────────────────────────────────

SimEngine::SimEngine(Team &homeTeam, Team &awayTeam, Display &display, int pointsToWin)
    : mHomeTeam(homeTeam),
      mAwayTeam(awayTeam),
      mDisplay(display),
      mPointsToWin(pointsToWin),
      mHomeSize(homeTeam.numPlayers()),
      mAwaySize(awayTeam.numPlayers()),
      mTotalPlayers(homeTeam.numPlayers() + awayTeam.numPlayers()),
      mBallPos{0,0},
      mHomeTeamScore(0),
      mAwayTeamScore(0),
      mFrameDelayMicros(INITIAL_SLOW_MICROS),
      mKickDelta{0,0},
      mKickSteps(0)
{
  // Clamp to the hard cap to avoid out-of-range access on mPlayers.
  if (mTotalPlayers > MAX_TOTAL_PLAYERS)
  {
    // Trim away team if combined size exceeds cap.
    mAwaySize = MAX_TOTAL_PLAYERS - mHomeSize;
    mTotalPlayers = MAX_TOTAL_PLAYERS;
  }
  std::memset(mField, 0, sizeof(mField));
}

// ──────────────────────────── run() ──────────────────────────────────────────

void SimEngine::run()
{
  mHomeTeam.onGameStart();
  mAwayTeam.onGameStart();

  bool gameOver = false;
  bool homeHasKickoff = mCoinFlip(mRng);

  while (!gameOver)
  {

    // ── Set up the next kickoff ──────────────────────────────────────────────
    initField();

    mHomeTeam.onKickoff(homeHasKickoff); // any preparation before choosing positions
    mAwayTeam.onKickoff(!homeHasKickoff);
    placePlayersForKickoff(homeHasKickoff); // place the players themselves

    mDisplay.render(buildGameState());

    bool goalScored = false;
    int overallCount = 0;
    Vec2 lastBallPos;
    mKickSteps = 0;

    // Resolution order: shuffled each tick for fair conflict breaking.
    std::vector<int> order(mTotalPlayers);
    std::iota(order.begin(), order.end(), 0);

    // ── Point loop: all players decide simultaneously, then world updates ─
    while (!goalScored)
    {
      lastBallPos = mBallPos; // remember ball position before player updates

      // ── 1. Collect all decisions against the current world snapshot ───
      std::vector<Action> actions(mTotalPlayers);
      for (int iPlayer = 0; iPlayer < mTotalPlayers; ++iPlayer)
      {
        Perception perception = buildPerception(iPlayer);
        if (iPlayer < mHomeSize)
          // Home agents: unmirrored frame, no rotation needed.
          actions[iPlayer] = mHomeTeam.player(iPlayer).decide(perception);
        else
          // Away agents: perception rotated 180°; action rotated back.
          actions[iPlayer] = rotateAction180(mAwayTeam.player(iPlayer - mHomeSize).decide(perception));
      }

      // ── 2. Shuffle resolution order to break conflicts fairly ─────────
      std::shuffle(order.begin(), order.end(), mRng);

      // ── 3. Apply all actions in shuffled order ────────────────────────
      for (int iPlayer : order)
        applyAction(iPlayer, actions[iPlayer]);

      // ── 4. Advance in-flight kick, BALL_SPEED cells per tick ──────────
      updateBall();

      // ── 5. Render the fully-updated world once ────────────────────────
      mDisplay.render(buildGameState());

      // ── 6. Check for a goal ───────────────────────────────────────────
      switch (updateScore())
      {
      case ScoreResult::HOME:
        goalScored = true;
        mHomeTeam.onGoalScored(true);
        mAwayTeam.onGoalScored(false);
        homeHasKickoff = false;
        break;
      case ScoreResult::AWAY:
        goalScored = true;
        mAwayTeam.onGoalScored(true);
        mHomeTeam.onGoalScored(false);
        homeHasKickoff = true;
        break;
      default:
        break; // no score so do nothing
      }

      // ── 7. Game stuck detection ────────────────────────────────────────
      if (!goalScored)
      {
        if (mBallPos == lastBallPos)
        { // Hard timeout: reset ball and penalise both teams.
          if (++overallCount >= GLOBAL_GAME_STUCK_TICKS)
          {
            std::printf("GLOBAL GAME STUCK - resetting\n");
            // resetBall();
            overallCount = 0;
            mHomeTeam.onGoalScored(false);
            mAwayTeam.onGoalScored(false);
            homeHasKickoff = !homeHasKickoff; // give the other team a chance
            break; // break out of while(!goalScored) to allow us to reset the field completely
          }
        }
        else
        {
          overallCount = 0;
        }
      }

      // ── 8. Handle keyboard input (speed / quit / pause / step) ───────
      if (auto key = mDisplay.pollInput(); key.has_value())
      {
        switch (*key)
        {
        case 'q':
          goalScored = gameOver = true;
          break;
        case 's':
          mFrameDelayMicros = std::min(mFrameDelayMicros * 2, MAX_SLOW_MICROS);
          break;
        case 'f':
          mFrameDelayMicros = std::max(mFrameDelayMicros / 2, 1);
          break;
        case ' ':
          mPaused = !mPaused;
          break;
        case 'n':
          mStepOnce = true;   // advance one tick while paused
          break;
        default:
          break;
        }
      }

      // ── 9. Pause spin-loop: block here until resumed or stepped ───────
      while (mPaused && !mStepOnce && !goalScored && !gameOver)
      {
        mDisplay.render(buildGameState());
        usleep(50000); // ~20 fps polling - plenty for human input
        if (auto key = mDisplay.pollInput(); key.has_value())
        {
          switch (*key)
          {
          case 'q':
            goalScored = gameOver = true;
            break;
          case ' ':
            mPaused = false;
            break;
          case 'n':
            mStepOnce = true;
            break;
          case 's':
            mFrameDelayMicros = std::min(mFrameDelayMicros * 2, MAX_SLOW_MICROS);
            break;
          case 'f':
            mFrameDelayMicros = std::max(mFrameDelayMicros / 2, 1);
            break;
          default:
            break;
          }
        }
      }
      mStepOnce = false;

      if (mFrameDelayMicros > 0 && !mPaused)
        usleep(mFrameDelayMicros);
    }

    // Check for game over.
    if (mHomeTeamScore >= mPointsToWin || mAwayTeamScore >= mPointsToWin)
      gameOver = true;
  }

  mHomeTeam.onGameOver();
  mAwayTeam.onGameOver();
  mDisplay.render(buildGameState());

  std::printf("\nFinal score:  %s %d  |  %s %d\n", mHomeTeam.name().c_str(), mHomeTeamScore, mAwayTeam.name().c_str(), mAwayTeamScore);
  if (mHomeTeamScore > mAwayTeamScore)
    std::printf("%s won!\n", mHomeTeam.name().c_str());
  else if (mAwayTeamScore > mHomeTeamScore)
    std::printf("%s won!\n", mAwayTeam.name().c_str());
  else
    std::printf("Draw!\n");
}

// ─────────────────────────── initField() ─────────────────────────────────────

void SimEngine::initField()
{
  // Clear field.
  for (int x = -HALF_FIELD.x; x <= HALF_FIELD.x; ++x)
    for (int y = -HALF_FIELD.y; y <= HALF_FIELD.y; ++y)
      fieldCell(x, y) = SimCell::EMPTY;

  // Left/right columns (x = ±HALF_FIELD.x) are goal lines.
  for (int y = -HALF_FIELD.y; y <= HALF_FIELD.y; ++y)
  {
    fieldCell(-HALF_FIELD.x, y) = SimCell::GOAL; // home goal (away team scores here)
    fieldCell( HALF_FIELD.x, y) = SimCell::GOAL; // away goal (home team scores here)
  }
  // Top/bottom rows (y = ±HALF_FIELD.y) are side walls (overwrites goal corners).
  for (int x = -HALF_FIELD.x; x <= HALF_FIELD.x; ++x)
  {
    fieldCell(x, -HALF_FIELD.y) = SimCell::WALL; // right/bottom wall
    fieldCell(x,  HALF_FIELD.y) = SimCell::WALL; // left/top wall
  }

  // Place ball at centre.
  placeBall();
}

// ─────────────────────────── placeBall() ─────────────────────────────────────

void SimEngine::placeBall()
{
  // Randomise ball y within the middle half of the field; keep x at centre (0).
  std::uniform_int_distribution<int> dist(-HALF_FIELD.y / 2, HALF_FIELD.y / 2);

  Vec2 ballPos = {0, dist(mRng)};

  // Find an empty y-position along the halfway line for the ball.
  while (fieldCell(ballPos) != SimCell::EMPTY)
    ballPos.y = dist(mRng);

  fieldCell(mBallPos) = SimCell::EMPTY;
  mBallPos = ballPos;
  fieldCell(mBallPos) = SimCell::BALL;
}

// ─────────────────────── placePlayersForKickoff() ────────────────────────────
// Called from run() after initField() and both teams' onKickoff().
// Loops over each agent, gets their desired kickoff position (in agent frame,
// which equals world frame for home, or negated world frame for away),
// enforces half-constraint and centre-spot rules, converts to world coords,
// and resolves collisions via nearest-empty search.

void SimEngine::placePlayersForKickoff(bool homeHasKickoff)
{
  // ── findNearestEmpty ──────────────────────────────────────────────────────
  // Find empty cell at current x location, searching outward in y.
  auto findNearestEmpty = [&](Vec2 pos) -> Vec2 {
    if (fieldCell(pos) == SimCell::EMPTY)
      return pos;

    Vec2 p1 = pos;
    Vec2 p2 = pos;

    for (int dy = 1; dy <= HALF_FIELD.y; ++dy)
    {
      p1.y = pos.y + dy;
      p2.y = pos.y - dy;

      if (withinField(p1) && (fieldCell(p1) == SimCell::EMPTY))
        return p1;
      else if (withinField(p2) && (fieldCell(p2) == SimCell::EMPTY))
        return p2;
    }

    return pos; // no free cell — should never happen
  };

  // ── Place one team ────────────────────────────────────────────────────────
  auto placeTeam = [&](bool isHome, bool homeHasKickoff)
  {
    bool centreLineUsed = false;

    Team& team = isHome ? mHomeTeam : mAwayTeam;
    int teamSize = isHome ? mHomeSize : mAwaySize;
    bool hasKickoff = isHome ? homeHasKickoff : !homeHasKickoff;

    for (int i = 0; i < teamSize; ++i)
    {
      Vec2 kickoffPos = team.player(i).getKickoffPosition(hasKickoff);

      if (hasKickoff)
      {
        // Enforce own-half constraint: agent x must be <= 0 (own half).
        kickoffPos.x = std::min(kickoffPos.x, 0);

        // Centre-line constraint: only one player may stand on x=0,
        // and that player must not be at y=0 (the ball spot).
        if (kickoffPos.x == 0)
        {
          if (kickoffPos.y == 0)
            kickoffPos.y = mCoinFlip(mRng) ? 1 : -1;
          if (!centreLineUsed)
            centreLineUsed = true;
          else
            kickoffPos.x = -1; // push back 1 cell
        }
      }
      else
      {
        // Non-kickoff team: must be at least 5 cells behind the halfway line.
        kickoffPos.x = std::min(kickoffPos.x, -5);
      }

      // Clamp to playable field bounds (excluding walls).
      kickoffPos.x = std::max(kickoffPos.x, -(HALF_FIELD.x - 1));
      kickoffPos.x = std::min(kickoffPos.x,   HALF_FIELD.x - 1);
      kickoffPos.y = std::max(kickoffPos.y, -(HALF_FIELD.y - 1));
      kickoffPos.y = std::min(kickoffPos.y,   HALF_FIELD.y - 1);

      // Convert agent frame to world frame and resolve collisions.
      Vec2 worldPos = agentToWorld(kickoffPos, isHome);

      worldPos = findNearestEmpty(worldPos);

      const int offset = isHome ? 0 : mHomeSize;
      mPlayers[offset + i] = worldPos;
      fieldCell(worldPos) = isHome ? SimCell::HOME_PLAYER : SimCell::AWAY_PLAYER;
    }
  };

  placeTeam(true, homeHasKickoff);
  placeTeam(false, homeHasKickoff);
}

// ─────────────────────────── resetBall() ─────────────────────────────────────

void SimEngine::resetBall()
{
  placeBall();
  mKickSteps = 0;
}

// ─────────────────────── buildPerception() ───────────────────────────────────

Perception SimEngine::buildPerception(int iPlayer) const
{
  bool isHome = (iPlayer < mHomeSize);

  // Real-world player position (world = home agent frame).
  Vec2 realPlayerPos = mPlayers[iPlayer];

  // Build the real-world local area (3×3 grid centred on the player).
  // dirToDelta() returns world-frame deltas: +x = FWD (home), +y = LEFT (home).
  std::array<Cell, 9> realLocal;
  for (int iDir = 0; iDir < 9; ++iDir)
  {
    if (iDir == static_cast<int>(Direction::CENTER))
    {
      realLocal[iDir] = Cell::SELF;
      continue;
    }

    Vec2 delta = dirToDelta(static_cast<Direction>(iDir));
    Vec2 newPos = realPlayerPos + delta;

    // Anything out of range is treated as a boundary.
    if (!withinField(newPos))
    {
      realLocal[iDir] = Cell::WALL;
      continue;
    }
    switch (fieldCell(newPos))
    {
    case SimCell::EMPTY:
      realLocal[iDir] = Cell::EMPTY;
      break;
    case SimCell::GOAL:
      realLocal[iDir] = Cell::GOAL;
      break;
    case SimCell::BALL:
      realLocal[iDir] = Cell::BALL;
      break;
    case SimCell::WALL:
      realLocal[iDir] = Cell::WALL;
      break;
    case SimCell::HOME_PLAYER:
      realLocal[iDir] = isHome ? Cell::TEAMMATE : Cell::OPPONENT;
      break;
    case SimCell::AWAY_PLAYER:
      realLocal[iDir] = isHome ? Cell::OPPONENT : Cell::TEAMMATE;
      break;
    }
  }

  // Compute ball direction; clamp to UNKNOWN if beyond the visible range.
  Vec2 ballDelta = mBallPos - realPlayerPos;
  int manhattanDist = (ballDelta.x < 0 ? -ballDelta.x : ballDelta.x)
                    + (ballDelta.y < 0 ? -ballDelta.y : ballDelta.y);
  bool ballVisible = (manhattanDist <= BALL_VISIBLE_DIST);
  Direction realBallDir = ballVisible ? computeBallDir(realPlayerPos, mBallPos) : Direction::UNKNOWN;
  int ballDistance = ballVisible ? manhattanDist : -1;

  if (isHome)
  {
    // Home agent: world frame IS the agent frame — no transformation needed.
    return Perception{realLocal, realBallDir, ballDistance, realPlayerPos};
  }
  else
  {
    // Away agent: rotate 180° so the agent perceives itself attacking FWD (+x).
    // localArea: index i maps to index 8-i (180° rotation preserves the layout).
    std::array<Cell, 9> rotLocal;
    for (int i = 0; i < 9; ++i)
      rotLocal[i] = realLocal[8 - i];

    Direction rotBallDir = rotateDir180(realBallDir);
    // Away agent position in its own frame: negate world coords.
    Vec2 awayAgentPos = {-realPlayerPos.x, -realPlayerPos.y};
    return Perception{rotLocal, rotBallDir, ballDistance, awayAgentPos};
  }
}

// ─────────────────────────── applyAction() ───────────────────────────────────
// action is always in real-world coordinates when this is called (the caller
// has already applied rotateAction180 for awayTeam agents).

void SimEngine::applyAction(int iPlayer, Action action)
{
  // Resolve KICK: find the ball in the real local area, set kick state,
  // then treat the move as "step toward the ball" (which pushes it once).
  if (action == Action::KICK)
  {
    int foundDir = -1;
    for (int iDir = 0; iDir < 9; ++iDir)
    {
      if (iDir == static_cast<int>(Direction::CENTER))
        continue;
      Vec2 delta = dirToDelta(static_cast<Direction>(iDir));
      Vec2 newPos = mPlayers[iPlayer] + delta;

      if (withinField(newPos) && fieldCell(newPos) == SimCell::BALL)
      {
        foundDir = iDir;
        break;
      }
    }

    if (foundDir >= 0) // we found the ball and can try to apply the kick
    {
      Vec2 delta = dirToDelta(static_cast<Direction>(foundDir));

      // ── Check for a contesting opponent beyond the ball ───────────
      Vec2 ballPos = mBallPos + delta;
      bool contested = (withinField(ballPos) && (fieldCell(ballPos) == SimCell::HOME_PLAYER || fieldCell(ballPos) == SimCell::AWAY_PLAYER));

      if (contested)
      {
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        if (chance(mRng) > KICK_CONTEST_SUCCESS)
        {
          // Kick fails entirely
          action = Action::DO_NOTHING;
        }
        else
        {
          // Kick succeeds but deflects — cannot go straight through opponent
          mKickDelta = deflectedDirection(delta);
          mKickSteps = KICK_DIST;
          action = deltaToAction(delta); // still step toward origin ball pos
        }
      }
      else
      {
        // Uncontested kick
        mKickDelta = delta;
        mKickSteps = KICK_DIST;
        action = deltaToAction(delta);
      }
    }
    else
    {
      action = Action::DO_NOTHING; // ball not adjacent to player, so cannot kick
    }
  }

  if (action == Action::DO_NOTHING)
    return;

  Vec2 playerDelta = actionToDelta(action);
  Vec2 newPlayerPos = mPlayers[iPlayer] + playerDelta;

  // Bounds check.
  if (!withinField(newPlayerPos))
    return;

  SimCell targetCell = fieldCell(newPlayerPos);

  bool canMove = false;

  if (targetCell == SimCell::EMPTY)
  {
    canMove = true;
  }
  else if (targetCell == SimCell::BALL)
  {
    // Can move if the cell beyond the ball is empty or a goal.
    Vec2 ballDelta = playerDelta;
    Vec2 newBallPos = mBallPos + ballDelta;

    if (withinField(newBallPos))
    {
      SimCell beyond = fieldCell(newBallPos);
      if (beyond == SimCell::EMPTY || beyond == SimCell::GOAL) // basic dribble
      {
        fieldCell(mBallPos) = SimCell::EMPTY;
        mBallPos = newBallPos;
        fieldCell(mBallPos) = SimCell::BALL;
        canMove = true;
      }
      else if (beyond == SimCell::HOME_PLAYER || beyond == SimCell::AWAY_PLAYER) // contested dribble
      {
        if (auto newPos = resolveDeflection(playerDelta, mBallPos))
        {
          fieldCell(mBallPos) = SimCell::EMPTY;
          mBallPos = *newPos;
          fieldCell(mBallPos) = SimCell::BALL;
          canMove = true; // player advances since ball escaped
        }
        // if nullopt: ball fully blocked, player cannot advance either
      }
    }
  }
  // if targetCell is a WALL, GOAL, or other players: cannot move.

  if (canMove)
  {
    // Clear old position.
    fieldCell(mPlayers[iPlayer]) = SimCell::EMPTY;
    mPlayers[iPlayer] = newPlayerPos;
    fieldCell(mPlayers[iPlayer]) = (iPlayer < mHomeSize) ? SimCell::HOME_PLAYER : SimCell::AWAY_PLAYER;
  }
}

Vec2 SimEngine::deflectedDirection(Vec2 delta)
{
  bool xDom = (std::abs(delta.x) >= std::abs(delta.y));
  std::array<Vec2, 4> candidates = xDom ? std::array<Vec2, 4>{{
                                              {delta.x, std::clamp(delta.y - 1, -1, 1)}, // diagonals
                                              {delta.x, std::clamp(delta.y + 1, -1, 1)},
                                              {0, -1}, // sides
                                              {0, 1},
                                          }}
                                        : std::array<Vec2, 4>{{
                                              {std::clamp(delta.x - 1, -1, 1), delta.y}, // diagonals
                                              {std::clamp(delta.x + 1, -1, 1), delta.y},
                                              {-1, 0}, // fwd/back
                                              {1, 0},
                                          }};

  std::discrete_distribution<int> prob({0.2, 0.2, 0.3, 0.3}); // probability: 2 diagonals, 2 sides
  return candidates[prob(mRng)];
}

// Call this when ballPos + delta is blocked so that the ball will be deflected.
// Returns the first valid ball position from deflection candidates,
// or nullopt if all are blocked.
std::optional<Vec2> SimEngine::resolveDeflection(Vec2 delta, Vec2 ballPos)
{
  bool xDominant = (std::abs(delta.x) >= std::abs(delta.y));

  std::array<Vec2, 4> candidates = xDominant ? std::array<Vec2, 4>{{
                                                   {-delta.x, delta.y},
                                                   {-delta.x, std::clamp(delta.y - 1, -1, 1)},
                                                   {0, -1},
                                                   {0, 1},
                                               }}
                                             : std::array<Vec2, 4>{{
                                                   {delta.x, -delta.y},
                                                   {std::clamp(delta.x - 1, -1, 1), -delta.y},
                                                   {-1, 0},
                                                   {1, 0},
                                               }};

  std::discrete_distribution<int> pick({0.2, 0.2, 0.3, 0.3});
  int first = pick(mRng);

  for (int i = 0; i < 4; ++i)
  {
    Vec2 newDelta = candidates[(first + i) % 4];
    Vec2 newBallPos = ballPos + newDelta;
    // margin={0,1} keeps ball away from side walls
    if (withinField(newBallPos, /*margin*/{0, 1}) && canBallEnter(fieldCell(newBallPos)))
      return newBallPos;
  }
  return std::nullopt;
}

// ─────────────────────────── updateBall() ───────────────────────────────────

void SimEngine::updateBall()
{
  for (int step = 0; step < BALL_SPEED && mKickSteps > 0; ++step)
  {
    Vec2 ballPos = mBallPos + mKickDelta;

    // Side-wall bounce: walls are at y = ±HALF_FIELD.y.
    if (ballPos.y <= -HALF_FIELD.y || ballPos.y >= HALF_FIELD.y)
    {
      mKickDelta.y = -mKickDelta.y;
      ballPos.y = mBallPos.y + mKickDelta.y;
    }

    // Safety guard: ball x should not escape during normal play (goals absorb).
    if (!withinField(ballPos))
    {
      mKickSteps = 0;
      return;
    }

    // Normal advance by one step.
    if (canBallEnter(fieldCell(ballPos)))
    {
      fieldCell(mBallPos) = SimCell::EMPTY;
      mBallPos = ballPos;
      fieldCell(mBallPos) = SimCell::BALL;
      --mKickSteps;
      continue; // onto next ballStep
    }

    // Normal advance wasn't possible so we must be blocked by a player
    // (walls already dealt with above) — try a deflection.

    if (auto newPos = resolveDeflection(mKickDelta, mBallPos))
    {
      mKickDelta = *newPos - mBallPos;
      fieldCell(mBallPos) = SimCell::EMPTY;
      mBallPos = *newPos;
      fieldCell(mBallPos) = SimCell::BALL;
      --mKickSteps;
    }
    else
    {
      mKickSteps = 0;
      return;
    }
  }
}

// ─────────────────────────── updateScore() ────────────────────────────────────
// Returns ScoreResult::HOME if home team scored (ball reached away goal at x=+HALF_FIELD.x),
// ScoreResult::AWAY if away team scored (ball reached home goal at x=-HALF_FIELD.x).

SimEngine::ScoreResult SimEngine::updateScore()
{
  if (mBallPos.x <= -HALF_FIELD.x)
  {
    ++mAwayTeamScore; // away team scores: ball in home goal
    return ScoreResult::AWAY;
  }
  else if (mBallPos.x >= HALF_FIELD.x)
  {
    ++mHomeTeamScore; // home team scores: ball in away goal
    return ScoreResult::HOME;
  }

  return ScoreResult::NONE;
}

// ─────────────────────────── buildGameState() ────────────────────────────────
// Positions are passed in world coords (origin at field centre).
// NcursesDisplay converts to screen coords before rendering.

GameState SimEngine::buildGameState() const
{
  GameState gs;
  gs.fieldWidth  = FIELD_LENGTH;
  gs.fieldHeight = FIELD_WIDTH;
  gs.ballPos     = mBallPos; // world coords
  gs.homeScore   = mHomeTeamScore;
  gs.awayScore   = mAwayTeamScore;
  gs.homeName    = mHomeTeam.name();
  gs.awayName    = mAwayTeam.name();
  gs.frameDelayMicros = mFrameDelayMicros;
  gs.paused      = mPaused;

  for (int i = 0; i < mHomeSize; ++i)
    gs.homePlayers.emplace_back(mPlayers[i]);
  for (int i = 0; i < mAwaySize; ++i)
    gs.awayPlayers.emplace_back(mPlayers[mHomeSize + i]);
  return gs;
}

// ────────────────────────── Geometry utilities ───────────────────────────────

Vec2 SimEngine::actionToDelta(Action a)
{
  // World frame: +x = FWD (home), +y = LEFT (home).
  // Action deltas are in world/home-agent frame.
  switch (a)
  {
  case Action::MOVE_FWD_LEFT:  return { 1,  1}; // FWD+LEFT:  dx=+1, dy=+1
  case Action::MOVE_FWD:       return { 1,  0}; // FWD:       dx=+1, dy= 0
  case Action::MOVE_FWD_RIGHT: return { 1, -1}; // FWD+RIGHT: dx=+1, dy=-1
  case Action::MOVE_LEFT:      return { 0,  1}; // LEFT:      dx= 0, dy=+1
  case Action::MOVE_RIGHT:     return { 0, -1}; // RIGHT:     dx= 0, dy=-1
  case Action::MOVE_BACK_LEFT:  return {-1,  1}; // BACK+LEFT:  dx=-1, dy=+1
  case Action::MOVE_BACK:       return {-1,  0}; // BACK:       dx=-1, dy= 0
  case Action::MOVE_BACK_RIGHT: return {-1, -1}; // BACK+RIGHT: dx=-1, dy=-1
  default: return {0, 0};
  }
}

// World frame: +x = FWD (home), +y = LEFT (home).
// Table rows: FWD (dx>0) → row 0, CENTER (dx=0) → row 1, BACK (dx<0) → row 2.
// Table cols: LEFT (dy>0) → col 0, CENTER (dy=0) → col 1, RIGHT (dy<0) → col 2.
Action SimEngine::deltaToAction(Vec2 delta)
{
  static constexpr Action TABLE[3][3] = {
    // col:  LEFT(dy>0)            CENTER(dy=0)        RIGHT(dy<0)
    { Action::MOVE_FWD_LEFT,  Action::MOVE_FWD,   Action::MOVE_FWD_RIGHT  }, // row: FWD  (dx>0)
    { Action::MOVE_LEFT,      Action::DO_NOTHING, Action::MOVE_RIGHT      }, // row: CENTER (dx=0)
    { Action::MOVE_BACK_LEFT, Action::MOVE_BACK,  Action::MOVE_BACK_RIGHT }, // row: BACK (dx<0)
  };
  int row = (delta.x > 0) ? 0 : (delta.x < 0) ? 2 : 1;
  int col = (delta.y > 0) ? 0 : (delta.y < 0) ? 2 : 1;
  return TABLE[row][col];
}

Vec2 SimEngine::dirToDelta(Direction d)
{
  // World frame: +x = FWD (home), +y = LEFT (home).
  // New Direction enum layout:
  //   FWD_LEFT(0)   FWD(1)   FWD_RIGHT(2)
  //      LEFT(3)  CENTER(4)     RIGHT(5)
  //   BACK_LEFT(6)  BACK(7)  BACK_RIGHT(8)
  switch (d)
  {
  case Direction::FWD_LEFT:   return { 1,  1};
  case Direction::FWD:        return { 1,  0};
  case Direction::FWD_RIGHT:  return { 1, -1};
  case Direction::LEFT:       return { 0,  1};
  case Direction::CENTER:     return { 0,  0};
  case Direction::RIGHT:      return { 0, -1};
  case Direction::BACK_LEFT:  return {-1,  1};
  case Direction::BACK:       return {-1,  0};
  case Direction::BACK_RIGHT: return {-1, -1};
  default:                    return { 0,  0};
  }
}

// World frame: +x = FWD (home), +y = LEFT (home).
// Table rows: FWD (dx>0) → row 0, CENTER (dx=0) → row 1, BACK (dx<0) → row 2.
// Table cols: LEFT (dy>0) → col 0, CENTER (dy=0) → col 1, RIGHT (dy<0) → col 2.
Direction SimEngine::deltaToDir(Vec2 delta)
{
  static constexpr Direction TABLE[3][3] = {
    // col:  LEFT(dy>0)             CENTER(dy=0)        RIGHT(dy<0)
    { Direction::FWD_LEFT,  Direction::FWD,    Direction::FWD_RIGHT  }, // row: FWD  (dx>0)
    { Direction::LEFT,      Direction::CENTER, Direction::RIGHT      }, // row: CENTER (dx=0)
    { Direction::BACK_LEFT, Direction::BACK,   Direction::BACK_RIGHT }, // row: BACK (dx<0)
  };
  int row = (delta.x > 0) ? 0 : (delta.x < 0) ? 2 : 1;
  int col = (delta.y > 0) ? 0 : (delta.y < 0) ? 2 : 1;
  return TABLE[row][col];
}

Direction SimEngine::rotateDir180(Direction d)
{
  // 180° rotation: FWD_LEFT↔BACK_RIGHT, FWD↔BACK, FWD_RIGHT↔BACK_LEFT,
  //                LEFT↔RIGHT, CENTER↔CENTER, UNKNOWN↔UNKNOWN.
  switch (d)
  {
  case Direction::FWD_LEFT:   return Direction::BACK_RIGHT;
  case Direction::FWD:        return Direction::BACK;
  case Direction::FWD_RIGHT:  return Direction::BACK_LEFT;
  case Direction::LEFT:       return Direction::RIGHT;
  case Direction::CENTER:     return Direction::CENTER;
  case Direction::RIGHT:      return Direction::LEFT;
  case Direction::BACK_LEFT:  return Direction::FWD_RIGHT;
  case Direction::BACK:       return Direction::FWD;
  case Direction::BACK_RIGHT: return Direction::FWD_LEFT;
  default:                    return Direction::UNKNOWN;
  }
}

Action SimEngine::rotateAction180(Action a)
{
  // 180° rotation: MOVE_FWD_LEFT↔MOVE_BACK_RIGHT, MOVE_FWD↔MOVE_BACK,
  //                MOVE_FWD_RIGHT↔MOVE_BACK_LEFT, MOVE_LEFT↔MOVE_RIGHT.
  // KICK and DO_NOTHING are invariant.
  switch (a)
  {
  case Action::MOVE_FWD_LEFT:   return Action::MOVE_BACK_RIGHT;
  case Action::MOVE_FWD:        return Action::MOVE_BACK;
  case Action::MOVE_FWD_RIGHT:  return Action::MOVE_BACK_LEFT;
  case Action::MOVE_LEFT:       return Action::MOVE_RIGHT;
  case Action::MOVE_RIGHT:      return Action::MOVE_LEFT;
  case Action::MOVE_BACK_LEFT:  return Action::MOVE_FWD_RIGHT;
  case Action::MOVE_BACK:       return Action::MOVE_FWD;
  case Action::MOVE_BACK_RIGHT: return Action::MOVE_FWD_LEFT;
  default: return a; // KICK and DO_NOTHING are invariant
  }
}

// Compute ball direction from player position (both in world coords).
Direction SimEngine::computeBallDir(Vec2 playerPos, Vec2 ballPos)
{
  return deltaToDir(ballPos - playerPos);
}

} // namespace Soccer
