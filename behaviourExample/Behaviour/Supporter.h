#pragma once

#include "HbsExampleTeamPlayers.h"

// ==========================================================================
// supporterTask
// ==========================================================================
CORO_DEFINE((HbsExamplePlayer, supporterTask),
  DEFINES_PARAMS((int,standoff,8)))
{
  CORO_BEGIN()

  while (true)
  {
    // we don't want to waste a tick if we don't need to, so check the ball is really missing first
    if (thePerception.ballDirection == Soccer::UNKNOWN)
      CORO_AWAIT_NO_TAIL(findBallTask({.searchXMin=FieldDimensions::xOwnGoalLine, .searchXMax=-1}));

    // failure sets no action, so we don't need to yield before moving on to next step
    if (goToTacticPoseTask({.tacticDistance=params.standoff}) != Hbs::FAILURE)
      CORO_YIELD();
  }

  CORO_END()
}


// ==========================================================================
// goToTacticPoseTask
// ==========================================================================
CORO_DEFINE((HbsExamplePlayer, goToTacticPoseTask),
  ARGS((int, tacticDistance)))
{
  using namespace Soccer;

  auto getTacticAction = [&]
  {
    switch (thePerception.ballDirection)
    {
    case BACK_LEFT: // fallthrough
    case BACK_RIGHT: // fallthrough
    case BACK: return MOVE_BACK;
    case LEFT: return MOVE_BACK_LEFT;
    case FWD_LEFT: return MOVE_LEFT;
    case RIGHT: return MOVE_BACK_RIGHT;
    case FWD_RIGHT: return MOVE_RIGHT;
    case FWD:
      if (thePerception.ballDistance > args.tacticDistance)
        return MOVE_FWD;
      else if (thePerception.ballDistance < args.tacticDistance)
        return MOVE_FWD;
      else
        return DO_NOTHING;
    default: return DO_NOTHING;
    }
  };

  CORO_BEGIN()

  while (true)
  {
    if (thePerception.ballDirection == UNKNOWN)
      CORO_FAILURE();

    setAction(avoidObstacle(getTacticAction()));
    CORO_YIELD();
  }

  CORO_END()
}
