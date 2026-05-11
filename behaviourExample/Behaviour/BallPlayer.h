#pragma once

#include "HbsExampleTeamPlayers.h"

// ==========================================================================
// ballPlayerTask
// ==========================================================================
CORO_DEFINE((HbsExamplePlayer, ballPlayerTask))
{
  CORO_BEGIN()

  // we use CORO_AWAIT_NO_TAIL for tasks that fail/succeed without any actuation
  // in the terminal status tick, incl. findBallTask and approachBallTask

  while (true)
  {
    // we don't want to waste a tick if we don't need to
    if (thePerception.ballDirection == Soccer::UNKNOWN)
      CORO_AWAIT_NO_TAIL(findBallTask({.searchXMin=0, .searchXMax=FieldDimensions::xOpponentGoalLine}));

    // approachBall can fail if ball goes UNKNOWN mid-approach
    // - just loop and find it again
    // - both failure and success are early exit without doing work, so we
    //   use NO_TAIL version to move along quickly
    CORO_AWAIT_NO_TAIL(approachBallTask({.desiredBallDir = Soccer::FWD}));

    // only kick if we are adjacent to the ball now
    if (adjacentBallDirection() != Soccer::UNKNOWN)
    {
      THIS_TASK.trace("Attempting KICK");
      setAction(Soccer::KICK);
      CORO_YIELD(); // we did something, so yield before repeating from the top
    }
    // else we couldn't kick, so loop from the top immediately (no yield)
  }

  CORO_END()
}


// ==========================================================================
// approachBallTask
// ==========================================================================
CORO_DEFINE((HbsExamplePlayer, approachBallTask),
  ARGS((Soccer::Direction, desiredBallDir)),
  VARS((Soccer::Direction, adjacentBallDir),
    (Soccer::Vec2, targetPos)))
{
  CORO_BEGIN()

  while (true)
  {
    if (thePerception.ballDirection == Soccer::UNKNOWN)
      CORO_FAILURE(); // fail so we can find the ball again

    if (thePerception.localArea[args.desiredBallDir] == Soccer::BALL)
      CORO_SUCCESS();

    // Where is the ball actually sitting relative to us?
    adjacentBallDir = adjacentBallDirection();

    if (adjacentBallDir == Soccer::UNKNOWN)
    {
      // not adjacent — step toward the ball
      // ballDirection gives us the direction, convert to a move
      // on each tick we re-evaluate, so we naturally track the ball
      targetPos = thePerception.agentPos + Soccer::directionToDelta(thePerception.ballDirection);
      if (moveToPointTask({.targetPos = targetPos}) == Hbs::FAILURE)
        CORO_FAILURE();
    }
    else
    {
      // ball is adjacent but not where we want it
      // we need to move so that the ball ends up at desiredBallDir
      // that means moving to: currentPos + adjacentBallDirDelta - desiredBallDirDelta
      targetPos = thePerception.agentPos + directionToDelta(adjacentBallDir) - directionToDelta(args.desiredBallDir);
      if (moveToPointTask({.targetPos = targetPos}) == Hbs::FAILURE)
        CORO_FAILURE();
    }

    CORO_YIELD();
  }

  CORO_END()
}


// ==========================================================================
// helpers
// ==========================================================================

Soccer::Direction HbsExamplePlayer::adjacentBallDirection()
{
  Soccer::Direction adjacentBallDir = Soccer::UNKNOWN;
  for (int i = 0; i < 9; ++i)
  {
    if (i == Soccer::CENTER)
      continue;
    if (thePerception.localArea[i] == Soccer::BALL)
    {
      adjacentBallDir = static_cast<Soccer::Direction>(i);
      break;
    }
  }
  return adjacentBallDir;
}