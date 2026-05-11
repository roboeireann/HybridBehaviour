#pragma once

#include "HybridBehaviour.h"

HSM_DECLARE(coordinateTeamTask);

CORO_DECLARE(ballPlayerTask);
CORO_DECLARE(approachBallTask, ARGS((Soccer::Direction, desiredBallDir)));
CORO_DECLARE(kickBallTask);

CORO_DECLARE(supporterTask);
CORO_DECLARE(goToTacticPoseTask, ARGS((int,tacticDistance)));

CORO_DECLARE(findBallTask, ARGS((int,searchXMin), (int,searchXMax)));

Soccer::Direction adjacentBallDirection(); // helper method

HSM_DECLARE(moveToPointTask, ARGS((Soccer::Vec2, targetPos)));

Soccer::Action avoidObstacle(Soccer::Action preferred); // ordinary helper method - not a task

CORO_DECLARE(teamCommsTask);
