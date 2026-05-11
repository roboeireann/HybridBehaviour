#pragma once

#include "HbsExampleTeamPlayers.h"

// ==========================================================================
// walkToPointTask
// ==========================================================================
// This could a coro or HSM. We make it a HSM because that better handles
// immediate resetting when the target changes

HSM_DEFINE((HbsExamplePlayer, moveToPointTask),
  ARGS((Soccer::Vec2, targetPos)))
{
  THIS_TASK.trace("targetPos = {{x={}, y={}}}", args.targetPos.x, args.targetPos.y);

  HSM_INITIAL_STATE(inactive)
  {
    HSM_TRANSITIONS
    {
      if (thePerception.agentPos == args.targetPos)
        HSM_GOTO(complete);
      else
        HSM_GOTO(moving);
    }
  }

  HSM_STATE(moving)
  {
    HSM_TRANSITIONS
    {
      if (thePerception.agentPos == args.targetPos)
        HSM_GOTO(complete);
    }

    Soccer::Vec2 delta = args.targetPos - thePerception.agentPos;
    Soccer::Vec2 absDelta {std::abs(delta.x), std::abs(delta.y)};

    Soccer::Action preferred;

    if (absDelta.x == absDelta.y)
      preferred = (delta.x > 0)
        ? (delta.y > 0 ? Soccer::MOVE_FWD_LEFT  : Soccer::MOVE_FWD_RIGHT)
        : (delta.y > 0 ? Soccer::MOVE_BACK_LEFT : Soccer::MOVE_BACK_RIGHT);
    else if (absDelta.x > absDelta.y)
      preferred = (delta.x > 0) ? Soccer::MOVE_FWD : Soccer::MOVE_BACK;
    else
      preferred = (delta.y > 0) ? Soccer::MOVE_LEFT : Soccer::MOVE_RIGHT;

    setAction(avoidObstacle(preferred));
  }

  HSM_SUCCESS_STATE(complete)
  {
    HSM_TRANSITIONS
    {
      if (thePerception.agentPos != args.targetPos)
        HSM_GOTO(moving);
    }
  }
}


// ordinary helper method (declared in container class)
Soccer::Action HbsExamplePlayer::avoidObstacle(Soccer::Action preferred)
{
  using namespace Soccer;

  Direction preferredDir = moveActionToDirection(preferred);
  Cell cellAhead = thePerception.localArea[preferredDir];

  if (cellAhead == EMPTY)
    return preferred;

  if ((cellAhead == GOAL) || (cellAhead == WALL))
  {
    // move parallel to boundary
    if (preferred == MOVE_FWD || preferred == MOVE_BACK)
      return (thePerception.agentPos.y >= 0) ? MOVE_RIGHT : MOVE_LEFT;
    else
      return (thePerception.agentPos.x >= 0) ? MOVE_BACK : MOVE_FWD;
  }

  // occupied - try alternatives preferring back of obstacle
  // ordered: lateral, diagonal-back, opposite lateral, back, do nothing
  static const std::array<std::array<Action, 4>, 10> fallbacks = {{
    /* MOVE_FWD_LEFT  */ {{ MOVE_LEFT,       MOVE_BACK_LEFT,  MOVE_FWD,       MOVE_BACK }},
    /* MOVE_FWD       */ {{ MOVE_FWD_LEFT,   MOVE_FWD_RIGHT,  MOVE_LEFT,      MOVE_RIGHT}},
    /* MOVE_FWD_RIGHT */ {{ MOVE_RIGHT,      MOVE_BACK_RIGHT, MOVE_FWD,       MOVE_BACK }},
    /* MOVE_LEFT      */ {{ MOVE_BACK_LEFT,  MOVE_FWD_LEFT,   MOVE_BACK,      MOVE_FWD  }},
    /* MOVE_RIGHT     */ {{ MOVE_BACK_RIGHT, MOVE_FWD_RIGHT,  MOVE_BACK,      MOVE_FWD  }},
    /* MOVE_BACK_LEFT */ {{ MOVE_BACK,       MOVE_LEFT,       MOVE_BACK_RIGHT,MOVE_FWD  }},
    /* MOVE_BACK      */ {{ MOVE_BACK_LEFT,  MOVE_BACK_RIGHT, MOVE_LEFT,      MOVE_RIGHT}},
    /* MOVE_BACK_RIGHT*/ {{ MOVE_BACK,       MOVE_RIGHT,      MOVE_BACK_LEFT, MOVE_FWD  }},
    /* KICK           */ {{ DO_NOTHING,      DO_NOTHING,      DO_NOTHING,     DO_NOTHING}},
    /* DO_NOTHING     */ {{ DO_NOTHING,      DO_NOTHING,      DO_NOTHING,     DO_NOTHING}},
  }};

  for (Action alt : fallbacks[preferred])
  {
    if (thePerception.localArea[static_cast<Direction>(alt)] == EMPTY)
      return alt;
  }

  return DO_NOTHING;
}