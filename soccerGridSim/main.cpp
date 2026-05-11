// main.cpp  –  Entry point for the standalone soccerGridSim 
// (This main does *not* demonstrate the HybridBehaviour framework.
// Look in behaviourExample instead)
//
// Usage:
//   soccer [options]
//     -d          headless / no display (faster, for benchmarking)
//     -p <n>      points needed to win  (default: 5)
//     -t <n>      players per team      (default: 2, max: 3)
//     -s <seed>   random seed           (default: time-based)

#include "SimpleChaseTeam.h"
#include "SimEngine.h"
#include "NcursesDisplay.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <unistd.h>

int main(int argc, char *argv[])
{
  bool headless = false;
  int pointsToWin = Soccer::DEFAULT_POINTS_TO_WIN;
  int playersPerTeam = 2;
  unsigned int seed = static_cast<unsigned int>(std::time(nullptr));

  int opt;
  while ((opt = getopt(argc, argv, "dp:t:s:")) != -1)
  {
    switch (opt)
    {
    case 'd':
      headless = true;
      break;
    case 'p':
      pointsToWin = std::atoi(optarg);
      if (pointsToWin <= 0)
      {
        std::fprintf(stderr, "Points must be > 0\n");
        return 1;
      }
      break;
    case 't':
      playersPerTeam = std::atoi(optarg);
      if (playersPerTeam < 1 || playersPerTeam > 3)
      {
        std::fprintf(stderr, "Players per team must be 1-3\n");
        return 1;
      }
      break;
    case 's':
      seed = static_cast<unsigned int>(std::atoi(optarg));
      break;
    default:
      std::fprintf(stderr, "Usage: %s [-d] [-p points] [-t players] [-s seed]\n", argv[0]);
      return 1;
    }
  }

  std::srand(seed);

  // ── Construct teams ───────────────────────────────────────────────────────
  Soccer::SimpleChaseTeam home("BlueChasers", playersPerTeam);
  Soccer::SimpleChaseTeam away("RedChasers", playersPerTeam);

  // ── Construct display ─────────────────────────────────────────────────────
  std::unique_ptr<Soccer::Display> display;
  if (headless)
    display = std::make_unique<Soccer::NullDisplay>();
  else
    display = std::make_unique<Soccer::NcursesDisplay>();

  // ── Run the game ──────────────────────────────────────────────────────────
  Soccer::SimEngine engine(home, away, *display, pointsToWin);
  engine.run();

  return 0;
}
