#pragma once

#include "HbsExampleTeamPlayers.h"

#include <algorithm> // std::clamp
#include <cmath> // std::round


// ==========================================================================
// findBallTask
// ==========================================================================
CORO_DEFINE((HbsExamplePlayer, findBallTask),
  ARGS((int, searchXMin), (int, searchXMax)),
  VARS((Soccer::Vec2, sweepPos, {0, 0}),
    (bool, regionScanned, false)))
{
  using namespace Soccer;

  static constexpr int LANE_STEP = BALL_VISIBLE_DIST * 2 - 1;
  static constexpr int Y_RIGHT = std::min(FieldDimensions::yRightSideLine + BALL_VISIBLE_DIST, 0);
  static constexpr int Y_LEFT = std::max(FieldDimensions::yLeftSideLine - BALL_VISIBLE_DIST, 0);

  // helper to compute the nearest lane x within search area
  auto nearestLaneX = [&]
  {
    int firstLane = args.searchXMin + BALL_VISIBLE_DIST;
    int lastLane  = args.searchXMax - BALL_VISIBLE_DIST;
    int idx = std::clamp((int)std::round((float)(thePerception.agentPos.x - firstLane) / LANE_STEP), 0,
                         (lastLane - firstLane) / LANE_STEP);
    return firstLane + idx * LANE_STEP;
  };

  CORO_BEGIN()

  // moveToPointTask succeeds with no actuation and cannot fail, so we use
  // CORO_AWAIT_NO_TAIL rather than CORO_AWAIT to avoid extra delays

  // snap to nearest lane before starting, staying at current y
  sweepPos = {nearestLaneX(), thePerception.agentPos.y};
  CORO_AWAIT_NO_TAIL(moveToPointTask({.targetPos = sweepPos}));

  // snap to nearest y-boundary, if we didn't encounter the ball already
  sweepPos = {sweepPos.x, thePerception.agentPos.y >= 0 ? Y_LEFT : Y_RIGHT};
  CORO_AWAIT_NO_TAIL(moveToPointTask({.targetPos = sweepPos}));

  // now begin scanning
  while (true)
  {
    if (thePerception.ballDirection != UNKNOWN)
      CORO_SUCCESS();

    if (regionScanned)
    {
      regionScanned = false; // until we complete another scan
      CORO_YIELD(); // ensure at least one YIELD per scan - in some circumstances this might be the only YIELD
    }

    // Phase 1 - sweep full width at current lane
    // direction determined by which side we're on - naturally alternates each lane
    CORO_STATE(sweep_sideways);

    sweepPos = {thePerception.agentPos.x, thePerception.agentPos.y > 0 ? Y_RIGHT : Y_LEFT};

    CORO_UNTIL(HBS_FINISHED(moveToPointTask({.targetPos = sweepPos})) || (thePerception.ballDirection != UNKNOWN), {});

    if (thePerception.ballDirection != UNKNOWN)
      CORO_SUCCESS();

    // Phase 2 - step to next lane, wrapping back to start of search area if needed
    CORO_STATE(step_to_next_lane);

    {
      int nextX = thePerception.agentPos.x + LANE_STEP;
      regionScanned = nextX > args.searchXMax - BALL_VISIBLE_DIST; // true if we wrap around
      sweepPos = {regionScanned ? args.searchXMin + BALL_VISIBLE_DIST : nextX, thePerception.agentPos.y};
    }

    CORO_UNTIL(HBS_FINISHED(moveToPointTask({.targetPos = sweepPos})) || (thePerception.ballDirection != UNKNOWN), {});
  }

  CORO_END()
}
