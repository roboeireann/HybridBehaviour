/**
 * The code in this file is just plumbing to connect players using the
 * HybridBehaviour framework to the soccerGridSim
 */
#pragma once

#include "Soccer.h"
#include "HybridBehaviour.h"

#include <array>
#include <memory>
#include <string>
#include <vector>



static constexpr int MAX_PLAYERS = 3;

struct PlayerSharedState {
    Soccer::Vec2 agentPos{0,0};
    int ballDistance = -1;
    unsigned lastPublishTick = 0;
};

using TeamSharedState = std::array<PlayerSharedState, MAX_PLAYERS>;

struct FieldDimensions
{
  static constexpr int xOwnGoalLine = -Soccer::FIELD_LENGTH / 2;
  static constexpr int xOpponentGoalLine = Soccer::FIELD_LENGTH / 2;
  static constexpr int yLeftSideLine = Soccer::FIELD_WIDTH / 2;
  static constexpr int yRightSideLine = -Soccer::FIELD_WIDTH / 2;
};

// ==========================================================================
// The player
// ==========================================================================

class HbsExamplePlayer : public Soccer::Agent
{
public:
  explicit HbsExamplePlayer(int playerIndex, int numPlayers, TeamSharedState &sharedState)
      : playerIndex(playerIndex),
        numPlayers(numPlayers),
        theSharedState(sharedState)
  {
  }

  Soccer::Action decide(const Soccer::Perception &p) override
  {
    simulatedTime += 100;

    theActionOutput = Soccer::DO_NOTHING; // reset each tick
    thePerception = p;

    HBS_BEGIN_TICK(simulatedTime);

    // execute root task - hardcoded here
    // Alternatively the task name could be loaded from a config file and
    // looked up in a task registry.
    rootTask();

    // after the behaviour has been ticked, display which tasks ran and
    // what state etc they are in
    displayTaskTrace();

    return theActionOutput;
  }

  virtual Soccer::Vec2 getKickoffPosition(bool hasKickoff) override
  {
    // FIXME
    if (hasKickoff)
    {
      switch (playerIndex)
      {
      case 0: return {-3,5};
      case 1: return {-3,-5};
      default: return {-10,0};        
      }
    }
    else 
    {
      switch (playerIndex)
      {
      case 0: return {-5,8};
      case 1: return {-5,-8};
      default: return {-15,0};        
      }
    }
  }

protected:
  enum Role
  {
    BALL_PLAYER,
    SUPPORTER,
    DEFENDER
  };

  int playerIndex;
  int numPlayers;
  TeamSharedState& theSharedState;

  Role theRole = SUPPORTER;

  Soccer::Perception thePerception;
  Soccer::Action theActionOutput;

  // define the shared environment for HBS tasks and behaviour
  HBS_ENV(&tickTrace);

  // Here we have an inline defined task - it is simple and could have been a basic
  // method, but we use an inline coro just to demonstrate inline definition
  CORO_DEFINE((rootTask))
  {
    CORO_BEGIN()

    while (true)
    {
      // NOTE: this is the behaviour for a single agent.
      // Each agent runs their own coordinateTeamTask based on what they
      // perceive and the shared info they may have received

      coordinateTeamTask(); // we name things as tasks if they have a context that persists

      if (theRole == BALL_PLAYER)
        ballPlayerTask();
      else
        supporterTask();

      teamCommsTask();

      THIS_TASK.trace("role={}", theRole==BALL_PLAYER ? "BALL_PLAYER" : "SUPPORTER");
      THIS_TASK.trace("agentPos={{x={},y={}}}", thePerception.agentPos.x, thePerception.agentPos.y);
      THIS_TASK.trace("ball dirn={}, dist={}", static_cast<int>(thePerception.ballDirection), thePerception.ballDistance);

      CORO_YIELD(); // important to yield or you'll hang the behaviour
    }

    CORO_END_FAILURE()
  }

  void setAction(Soccer::Action action) { theActionOutput = action; }

  // include externally declared behaviour tasks into the player behaviour file
  #include "Behaviour/BehaviourDeclarations.h"

private:
  unsigned simulatedTime = 1000;
  Hbs::TickTrace tickTrace;

  void* mTraceWindow{nullptr}; // WINDOW* — opaque here to keep ncurses out of the header

  void displayTaskTrace();
};

// ==========================================================================
// The team
// ==========================================================================

class HbsExampleTeam : public Soccer::Team
{
public:
  explicit HbsExampleTeam(std::string teamName, int numPlayers)
      : mName(std::move(teamName))
  {
    for (int i = 0; i < numPlayers; ++i)
      mAgents.emplace_back(std::make_unique<HbsExamplePlayer>(i, numPlayers, mSharedState));
  }

  std::string name() const override { return mName; }
  int numPlayers() const override { return static_cast<int>(mAgents.size()); }
  Soccer::Agent &player(int id) override { return *mAgents[id]; } // sim calls to access individual agents

private:
  std::string mName;
  std::vector<std::unique_ptr<HbsExamplePlayer>> mAgents;
  TeamSharedState mSharedState;
};
