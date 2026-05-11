#include "Soccer.h"

namespace Soccer 
{

Action directionToMoveAction(Direction d)
{
  switch (d)
  {
  case Direction::FWD_LEFT:  return Action::MOVE_FWD_LEFT;
  case Direction::FWD:       return Action::MOVE_FWD;
  case Direction::FWD_RIGHT: return Action::MOVE_FWD_RIGHT;
  case Direction::LEFT:      return Action::MOVE_LEFT;
  case Direction::RIGHT:     return Action::MOVE_RIGHT;
  case Direction::BACK_LEFT:  return Action::MOVE_BACK_LEFT;
  case Direction::BACK:       return Action::MOVE_BACK;
  case Direction::BACK_RIGHT: return Action::MOVE_BACK_RIGHT;
  case Direction::CENTER:  // no movement required
  case Direction::UNKNOWN: // should not occur if a valid direction provided
  default: return Action::DO_NOTHING;
  }
}

Direction moveActionToDirection(Action a)
{
  switch (a)
  {
  case Action::MOVE_FWD_LEFT:  return Direction::FWD_LEFT;
  case Action::MOVE_FWD:       return Direction::FWD;
  case Action::MOVE_FWD_RIGHT: return Direction::FWD_RIGHT;
  case Action::MOVE_LEFT:      return Direction::LEFT;
  case Action::MOVE_RIGHT:     return Direction::RIGHT;
  case Action::MOVE_BACK_LEFT:  return Direction::BACK_LEFT;
  case Action::MOVE_BACK:       return Direction::BACK;
  case Action::MOVE_BACK_RIGHT: return Direction::BACK_RIGHT;
  default: return Direction::CENTER;
  }
}

/// convert a direction to a delta in the agent coord frame
/// Agent frame: +x = FWD, +y = LEFT
Vec2 directionToDelta(Direction d)
{
  switch (d)
  {
  case Direction::FWD_LEFT:   return { 1,  1};
  case Direction::FWD:        return { 1,  0};
  case Direction::FWD_RIGHT:  return { 1, -1};
  case Direction::LEFT:       return { 0,  1};
  case Direction::RIGHT:      return { 0, -1};
  case Direction::BACK_LEFT:  return {-1,  1};
  case Direction::BACK:       return {-1,  0};
  case Direction::BACK_RIGHT: return {-1, -1};
  case Direction::CENTER:  // no delta
  case Direction::UNKNOWN: // should not occur if a valid direction provided
  default: return {0, 0};
  }
}

/// convert a delta in agent coord frame to a direction
/// Agent frame: +x = FWD, +y = LEFT
Direction deltaToDirection(Vec2 delta)
{
  // Clamp to {-1, 0, 1} then look up in the 3×3 table.
  // row: delta.x > 0 => FWD (0), delta.x == 0 => CENTER (1), delta.x < 0 => BACK (2)
  // col: delta.y > 0 => LEFT (0), delta.y == 0 => CENTER (1), delta.y < 0 => RIGHT (2)
  static constexpr Direction TABLE[3][3] = {
      // col:  LEFT(y>0)          CENTER(y=0)       RIGHT(y<0)
      {Direction::FWD_LEFT,   Direction::FWD,    Direction::FWD_RIGHT},  // row: FWD (x>0)
      {Direction::LEFT,       Direction::CENTER, Direction::RIGHT},       // row: CENTER (x=0)
      {Direction::BACK_LEFT,  Direction::BACK,   Direction::BACK_RIGHT}  // row: BACK (x<0)
  };
  int row = (delta.x > 0) ? 0 : (delta.x < 0) ? 2 : 1;
  int col = (delta.y > 0) ? 0 : (delta.y < 0) ? 2 : 1;
  return TABLE[row][col];
}


Direction findAdjacentBall(const std::array<Cell, 9>& localArea)
{
  Direction adjacentBallDir = UNKNOWN;
  for (int i = 0; i < 9; ++i)
  {
    if (i == CENTER)
      continue;
    if (localArea[i] == BALL)
    {
      adjacentBallDir = static_cast<Soccer::Direction>(i);
      break;
    }
  }
  return adjacentBallDir;
}



} // namespace Soccer
