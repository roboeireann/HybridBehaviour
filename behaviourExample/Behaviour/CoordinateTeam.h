#pragma once

#include "HbsExampleTeamPlayers.h"

HSM_DEFINE((HbsExamplePlayer, coordinateTeamTask), 
          VARS((int, disputeTicks, 0), 
            (int, disputedBallPlayerIdx, -1),
            (int, requiredTicks, 0)),
          DEFINES_PARAMS(
            (int, fastSwitchDistance, 6), // if one agent closer to ball by this much role switches quickly
            (int, maxDisputeTicks, 5), // if one agent closer to ball by less than fast switch, we need stability first
            (unsigned, maxStaleness, 10)
          ))
{
  // helper lambdas
  auto dataValid = [&](int idx)
  { return (THIS_TASK.currentTick() - theSharedState[idx].lastPublishTick) <= params.maxStaleness; };

  // returns index of closest player to ball, or -1 if nobody has valid distance
  auto closestToBall = [&]
  {
    int bestIdx = -1;
    int bestDist = INT_MAX;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
      int d = theSharedState[i].ballDistance;
      if (dataValid(i) && (d != -1) && (d < bestDist))
      {
        bestDist = d;
        bestIdx = i;
      }
    }
    return bestIdx;
  };

  // linear interpolation: requiredTicks is 0 at fastSwitchDistance delta, maxDisputeTicks at delta 0
  auto computeRequiredTicks = [&](int closest)
  {
    if (closest == -1)
      return 0;
    int selfDist = thePerception.ballDistance == -1 ? INT_MAX : thePerception.ballDistance;
    int otherDist = theSharedState[closest].ballDistance;
    if (selfDist == INT_MAX || otherDist == -1)
      return 0;
    int delta = std::abs(selfDist - otherDist);
    if (delta >= params.fastSwitchDistance)
      return 0;
    return params.maxDisputeTicks - (params.maxDisputeTicks * delta) / params.fastSwitchDistance;
  };


  HSM_INITIAL_STATE(assign_initial_role)
  {
    HSM_ON_ENTRY
    {
      int closest = closestToBall();
      theRole = (closest == playerIndex || closest == -1) ? Role::BALL_PLAYER : Role::SUPPORTER;
    }
    HSM_TRANSITIONS { HSM_GOTO(role_assigned); }
  }

  HSM_STATE(role_assigned)
  {
    int closest = closestToBall();

    HSM_TRANSITIONS
    {
      if ((theRole == Role::BALL_PLAYER) && (closest != -1) && (closest != playerIndex))
      {
        disputedBallPlayerIdx = closest;
        HSM_GOTO(role_disputed);
      }
      else if ((theRole == Role::SUPPORTER) && (closest == playerIndex))
      {
        disputedBallPlayerIdx = playerIndex;
        HSM_GOTO(role_disputed);
      }
    }
  }

  HSM_STATE(role_disputed)
  {
    int closest = closestToBall();

    HSM_ON_ENTRY
    {
      requiredTicks = computeRequiredTicks(closest);
      disputeTicks = 1;
    }

    HSM_TRANSITIONS
    {
      if (closest == -1)
        HSM_GOTO(role_assigned); // can't observe dispute, keep current roles
      if ((theRole == Role::BALL_PLAYER) && (closest == playerIndex))
        HSM_GOTO(role_assigned); // we're closest again, dispute over
      else if ((theRole == Role::SUPPORTER) && (closest != playerIndex))
        HSM_GOTO(role_assigned); // current ball player is closest, dispute over
      else if (disputeTicks >= requiredTicks)
      {
        // we can allow some "action" code associated with a transition (UML2 statechart semantics)
        THIS_TASK.trace("disputedBallPlayerIdx={}, playerIndex={}", disputedBallPlayerIdx, playerIndex);
        theRole = (disputedBallPlayerIdx == playerIndex) ? Role::BALL_PLAYER : Role::SUPPORTER;
        HSM_GOTO(role_assigned);
      }
    }

    // body of state
    // guaranteed that closest is a valid player number at this point

    if (closest != disputedBallPlayerIdx)
    {
      disputedBallPlayerIdx = closest; // closest changed - update but reset counter
      requiredTicks = computeRequiredTicks(closest); // recompute for new candidate
      disputeTicks = 1;
    }
    else
      ++disputeTicks;

    THIS_TASK.trace("closest={}, disputed={}, ticks={}/{}", closest, disputedBallPlayerIdx, disputeTicks, requiredTicks);
  }
}
