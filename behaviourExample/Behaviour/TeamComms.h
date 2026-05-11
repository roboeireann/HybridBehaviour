/**
 * Simulate team communication by publishing to a shared (between players)
 * data area
 */
#pragma once

#include "HbsExampleTeamPlayers.h"


CORO_DEFINE((HbsExamplePlayer, teamCommsTask),
  VARS((unsigned, lastPublishTick, 0)),
  DEFINES_PARAMS((unsigned, publishInterval, 5)))
{
  CORO_BEGIN()

  while (true)
  {
    if ((lastPublishTick == 0) || (THIS_TASK.ticksSince(lastPublishTick) >= params.publishInterval))
    {
      lastPublishTick = THIS_TASK.currentTick();
      theSharedState[playerIndex] = {thePerception.agentPos, thePerception.ballDistance,
                                     lastPublishTick};
    }
    CORO_YIELD();
  }

  CORO_END()
}