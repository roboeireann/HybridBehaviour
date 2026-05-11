// SimpleChaseTeam.h  –  A minimal example team intended to demonstrate the
//                  Soccer::Agent / Soccer::Team API.
//
// Strategy: each agent moves directly toward the ball.
//            If the ball is adjacent in a forward arc, kick it.
//
// This is a very naive behaviour:
//   1. Only kick when the ball is in a forward arc (FWD_RIGHT, FWD, FWD_LEFT).
//      Kicking sideways sends the ball into the boundary; kicking backward
//      scores an own goal.  In those cases the agent repositions instead.
//   2. When the ball is purely to the LEFT or RIGHT and that cell is blocked
//      (occupied by another player), the agent steps BACK (away from goal).
//      This places the ball in the FWD_LEFT or FWD_RIGHT arc on the next turn,
//      breaking the lateral queue.
//   3. When ballDirection == UNKNOWN (ball is beyond BALL_VISIBLE_DIST) the
//      agent advances FWD, which moves it toward the opponent's goal and
//      generally toward the centre of the field where the ball is likely to be.
//
// Coordinate reminder (agent frame, both teams always attack FWD = +x):
//   FWD_LEFT   FWD   FWD_RIGHT
//      LEFT  CENTER     RIGHT
//   BACK_LEFT  BACK  BACK_RIGHT
//
// Positive x = FWD (toward opponent's goal); positive y = LEFT.
// Both teams share the same agent logic; the engine's mirroring ensures
// that each team always perceives itself attacking in the FWD direction.

#pragma once

#include "Soccer.h"

#include <memory>
#include <string>
#include <vector>
#include <random>

namespace Soccer
{

// ─────────────────────────── SimpleChaseAgent ──────────────────────────────────────

class SimpleChaseAgent : public Agent
{
public:
  SimpleChaseAgent(int playerIndex) : playerIndex(playerIndex) {}

  Action decide(const Perception &p) override
  {
    Direction adjacentBallDir = findAdjacentBall(p.localArea);
    Direction preferredDir;

    // if the ball is in the forward arc, kick it.
    // otherwise try to move somewhere behind the ball (putting it into forward arc)
    switch (adjacentBallDir)
    {
      case FWD_LEFT: 
      case FWD:
      case FWD_RIGHT: 
        return KICK;

      case LEFT: preferredDir = BACK_LEFT; break;
      case RIGHT: preferredDir = BACK_RIGHT; break;

      case BACK_LEFT:
      case BACK_RIGHT:
        preferredDir = BACK; break;

      case BACK: preferredDir = coinFlip(rng) ? BACK_LEFT : BACK_RIGHT; break;
      default: preferredDir = CENTER; break; // i.e. don't move
    }

    if (preferredDir != CENTER) // did we find an adjacent ball?
    {
      // is the cell in our desired direction free
      // yes - go in that direction, no - just give up
      if (p.localArea[preferredDir] == Cell::EMPTY)
        return directionToMoveAction(preferredDir);
      else
        return DO_NOTHING;
    }

    // if we get here, the ball was not adjacent.
    // if the Ball is not visible, do a random walk to try and find it
    if (p.ballDirection == Direction::UNKNOWN)
    {
      do
      {
        preferredDir = static_cast<Direction>(randomDir(rng));
      } 
      while (p.localArea[preferredDir] != Cell::EMPTY);

      return directionToMoveAction(preferredDir);
    }

    auto checkDirections = [&](Direction dir1, Direction dir2 = UNKNOWN, Direction dir3 = UNKNOWN) -> Direction
    {
      if (p.localArea[dir1] == Cell::EMPTY)
        return dir1;
      else if (dir2 != UNKNOWN && p.localArea[dir2] == Cell::EMPTY)
        return dir2;
      else if (dir3 != UNKNOWN && p.localArea[dir3] == Cell::EMPTY)
        return dir3;
      else
        return UNKNOWN;
    };

    // if we get here, we have a general direction to the ball
    // try to walk in that direction, or move backwards in case the way is blocked
    preferredDir = p.ballDirection;
    if (p.localArea[preferredDir] != Cell::EMPTY)
    {
      switch (preferredDir)
      {
        case FWD_LEFT: preferredDir = checkDirections(LEFT, BACK_LEFT, BACK); break;
        case FWD: preferredDir = checkDirections(FWD_LEFT, FWD_RIGHT); break;
        case FWD_RIGHT: preferredDir = checkDirections(RIGHT, BACK_RIGHT, BACK); break;

        case LEFT: preferredDir = checkDirections(BACK_LEFT, BACK); break;
        case RIGHT: preferredDir = checkDirections(BACK_RIGHT, BACK); break;

        case BACK_LEFT: preferredDir = checkDirections(BACK, BACK_RIGHT); break;
        case BACK_RIGHT: preferredDir = checkDirections(BACK, BACK_LEFT); break;

        default: preferredDir = UNKNOWN;
      }
    }

    // do our best to choose an action - it might end up being do nothing if
    // the way was blocked even after checking fallback positions
    return directionToMoveAction(p.ballDirection);
  }

  virtual Vec2 getKickoffPosition([[maybe_unused]] bool hasKickoff) override
  {
    if (hasKickoff)
    {
      switch (playerIndex)
      {
      case 0: return {-3,5};
      case 1: return {-6,-5};
      default: return {-10,0};        
      }
    }
    else 
    {
      switch (playerIndex)
      {
      case 0: return {-5,8};
      case 1: return {-10,-8};
      default: return {-15,0};        
      }
    }
  }

private:
  int playerIndex;
  std::mt19937 rng{std::random_device{}()};
  std::bernoulli_distribution coinFlip{0.5};
  std::uniform_int_distribution<int> randomDir{0, 8};
};

// ─────────────────────────── SimpleChaseTeam ───────────────────────────────────────

class SimpleChaseTeam : public Team
{
public:
  explicit SimpleChaseTeam(std::string teamName, int numPlayers)
      : mName(std::move(teamName))
  {
    for (int i = 0; i < numPlayers; ++i)
      mAgents.emplace_back(std::make_unique<SimpleChaseAgent>(i));
  }

  std::string name() const override { return mName; }
  int numPlayers() const override { return static_cast<int>(mAgents.size()); }
  Agent &player(int id) override { return *mAgents[id]; }


private:
  std::string mName;
  std::vector<std::unique_ptr<SimpleChaseAgent>> mAgents;
};

} // namespace Soccer
