# HybridBehaviour

An open-source C++20 framework for real-time robot behaviour control. HybridBehaviour unifies three complementary reactive-control paradigms under a single tick-driven execution model:

- **Coroutine-based tasks** — sequential behaviour written in a natural top-to-bottom style
- **Hierarchical State Machines (HSMs)** — non-linear, transition-driven behaviour
- **Behaviour Tree combinators** — prioritised fallback and ordered sequencing

Rather than forcing every behaviour into one paradigm, the framework lets you choose the most appropriate abstraction for each concern and mix them freely. It is designed for real-time execution at 30–60 Hz or higher and has been grounded in RoboCup competition experience since 2012.

## Getting started

- **[Installation](https://github.com/roboeireann/HybridBehaviour/wiki/Installation)** — prerequisites, cloning, building, and running the examples
- **[Core Concepts](https://github.com/roboeireann/HybridBehaviour/wiki/Core-Concepts)** — the tick model, task status, and auto-reset before you write your first task
- **[Worked Example](https://github.com/roboeireann/HybridBehaviour/wiki/Worked-Example)** — a complete multi-agent soccer example using all three abstractions

Full documentation is in the **[wiki](https://github.com/roboeireann/HybridBehaviour/wiki)**:

<!-- Only uncomment this if the paper is accepted
## Citing this work

If you use HybridBehaviour in your research, please cite:

```
Villing, R. (2026). An Open-Source Hybrid Behaviour Framework Integrating Coroutines,
HSMs, and Reactive Control. RoboCup Symposium 2026.
```
-->

## Status

The framework is under active development. Known issues are being addressed
and a stable release is expected imminently. If you encounter a problem, please
check the [Issues](https://github.com/roboeireann/HybridBehaviour/issues)
page before assuming it is a fundamental limitation of the approach.

# Acknowledgements

See [Acknowledgments](Acknowledgements.md) for details
