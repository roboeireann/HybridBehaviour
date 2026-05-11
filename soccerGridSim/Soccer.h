  #pragma once

// soccer.h  –  Public API for soccer agent implementations.
//
// Teams subclass Team and return Agent subclasses from player().
// The engine calls decide() on each agent every turn and handles
// all mirroring so that both teams always perceive themselves as
// attacking in the FWD direction (left-to-right on screen for the home team).
//
// Coordinate system (agent's own reference frame):
//
//   ------------- left side wall ------------
//   |                                       |
//   |                   ^ Y                 |
//   | own               |                   |opponent
//   | goal            (0,0) --> X           |goal
//   | line                                  |line
//   |        attacking direction    ====>   |
//   |                                       |
//   ------------ right side wall ------------

//
// +x = FWD  (toward opponent's goal)
// +y = LEFT (toward the left side wall from the agent's perspective)
//
// The unmirrored coordinate system corresponds to the home team, which
// attacks left-to-right on screen.  The away team's perception is
// rotated 180° by the engine so that it also sees positive-x as FWD.
//
// The sim world coordinate system uses the same +y = LEFT convention
// (y increases toward the top of the screen).

#include <array>
#include <string>

namespace Soccer
{

// ─────────────────────────── Field constants ─────────────────────────────────

static constexpr int FIELD_LENGTH = 67; // length as seen by players => width as rendered on screen
static constexpr int FIELD_WIDTH = 23; // width as seen by players => height as rendered on screen
static_assert(FIELD_LENGTH % 2 == 1, "FIELD_LENGTH must be odd");
static_assert(FIELD_WIDTH  % 2 == 1, "FIELD_WIDTH must be odd");


static constexpr int KICK_DIST = 12;
static constexpr int DEFAULT_POINTS_TO_WIN = 5;
// Maximum Manhattan distance at which an agent can perceive the ball's true
// direction.  If |dx| + |dy| > BALL_VISIBLE_DIST the engine reports UNKNOWN.
static constexpr int BALL_VISIBLE_DIST = 20;

// ────────────────────────────── Cell types ───────────────────────────────────
// What an agent can observe in each cell of its 3×3 local area.
enum Cell
{
  EMPTY,
  GOAL, // the goal column (x=0 or x=FIELD_LENGTH-1 in world coords)
  BALL,
  WALL, // left/right edge of the field (agent perspective) - it is a wall like indoor soccer
  TEAMMATE,
  OPPONENT,
  SELF // the centre cell – always the calling agent's own position
};

// ────────────────────────── Direction / local-area index ─────────────────────
// The 3×3 local area is laid out from the agent's own perspective,
// where FWD (+x) is toward the opponent's goal and LEFT (+y) is to the left:
//
//   FWD_LEFT(0)   FWD(1)   FWD_RIGHT(2)
//      LEFT(3)  CENTER(4)     RIGHT(5)
//   BACK_LEFT(6)  BACK(7)  BACK_RIGHT(8)
//
// Both teams always perceive FWD as the direction toward the opponent's goal.
// DIR is also used for Perception::ballDirection.
enum Direction : int
{
  FWD_LEFT  = 0,
  FWD       = 1,
  FWD_RIGHT = 2,
  LEFT      = 3,
  CENTER    = 4,
  RIGHT     = 5,
  BACK_LEFT  = 6,
  BACK       = 7,
  BACK_RIGHT = 8,
  UNKNOWN = 9  // ball is beyond BALL_VISIBLE_DIST; true direction not known
};

// ─────────────────────────────── Actions ─────────────────────────────────────
// MOVE_* moves the agent one cell in the named direction (agent's own frame).
// If the ball occupies that cell and the cell beyond is empty or a goal,
// the ball is pushed along too.
// KICK propels the ball KICK_DIST cells in whichever direction it currently
// sits relative to the agent. The agent itself does not move on a kick turn.
// DO_NOTHING leaves everything in place.
enum Action
{
  MOVE_FWD_LEFT,
  MOVE_FWD,
  MOVE_FWD_RIGHT,
  MOVE_LEFT,
  MOVE_RIGHT,
  MOVE_BACK_LEFT,
  MOVE_BACK,
  MOVE_BACK_RIGHT,
  DO_NOTHING,
  KICK
};


// ────────────────────────────── Vec2 ───────────────────────────────────
// a simple representation of a point, a delta, a distance, a speed in
// 2D coordinates
struct Vec2
{
  int x;
  int y;

  auto operator<=>(const Vec2&) const = default;
  Vec2& operator+=(const Vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }  
  Vec2& operator-=(const Vec2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
  Vec2 operator+(const Vec2& rhs) const { Vec2 tmp = *this; return tmp += rhs; }
  Vec2 operator-(const Vec2& rhs) const { Vec2 tmp = *this; return tmp -= rhs; }  
};

// ────────────────────────────── Perception ───────────────────────────────────
// All information an agent is permitted to observe on its turn.
// The engine guarantees that home and away agents receive symmetrical
// perceptions – both teams always appear to be attacking in the FWD direction.
//
// x, y are in the agent's own reference frame with (0, 0) at the centre of
// the field.  Positive x is toward FWD (opponent's goal); positive y is toward LEFT.
struct Perception
{
  std::array<Cell, 9> localArea; // indexed by static_cast<int>(Direction)
  Direction ballDirection;       // direction from this agent toward the ball
  int ballDistance;              // Manhattan distance to the ball; -1 if beyond BALL_VISIBLE_DIST
  Vec2 agentPos;                  // agent's position; (0,0) = field centre
};

// ──────────────────────────── Agent interface ────────────────────────────────
class Agent
{
public:
  // Called once per turn.  Return the action this agent wishes to take.
  virtual Action decide(const Perception &) = 0;


  // Return this agent's desired kickoff position in the agent's own reference
  // frame ((0,0) = field centre, +x = FWD toward opponent's goal, +y = LEFT).
  //
  // Called by the engine once per agent before each kickoff, after
  // onKickoff() has been called on the team. Teams that need
  // coordinated (non-hardcoded) formation placement should compute positions in
  // onKickoff() and return the pre-computed result here.
  //
  // Constraints the engine enforces:
  //   - kickoff team: x must be <= 0 (own half); one player may be at x=0 but not y=0
  //   - non kickoff team: x must be <= -5 (set back from halfway line)
  //   - position must be within the playable field
  //   - collisions between players are resolved by the engine in FCFS order
  virtual Vec2 getKickoffPosition([[maybe_unused]] bool hasKickoff) = 0;

  // Lifecycle hooks.  Default implementations do nothing.
  virtual void onGameStart() {}
  virtual void onKickoff([[maybe_unused]] bool hasKickoff) {}
  virtual void onGoalScored([[maybe_unused]] bool ownTeamScored) {}
  virtual void onGameOver() {}  

  virtual ~Agent() = default;
};

// ──────────────────────────── Team interface ─────────────────────────────────
class Team
{
public:
  virtual std::string name() const = 0;
  virtual int numPlayers() const = 0;      // players per team
  virtual Agent &player(int id) = 0; // id: 0 .. size()-1

  // Default lifecycle implementations forward to every agent.
  // Override at the team level if you need team-wide coordination.
  virtual void onGameStart()
  {
    for (int i = 0; i < numPlayers(); ++i)
      player(i).onGameStart();
  }

  virtual void onKickoff(bool hasKickoff)
  {
    for (int i = 0; i < numPlayers(); ++i)
      player(i).onKickoff(hasKickoff);
  }
  virtual void onGoalScored(bool ownTeamScored)
  {
    for (int i = 0; i < numPlayers(); ++i)
      player(i).onGoalScored(ownTeamScored);
  }
  virtual void onGameOver()
  {
    for (int i = 0; i < numPlayers(); ++i)
      player(i).onGameOver();
  }

  virtual ~Team() = default;
};


// ──────────────────────────── helpers ─────────────────────────────────

// convert a direction to the equivalent move action
Action directionToMoveAction(Direction d);
Direction moveActionToDirection(Action a);

/// convert a direction to a delta in the agent coord frame
Vec2 directionToDelta(Direction d);
/// convert a delta in agent coord frame to a direction
Direction deltaToDirection(Vec2 delta);

Direction findAdjacentBall(const std::array<Cell, 9>& localArea);


} // namespace Soccer
