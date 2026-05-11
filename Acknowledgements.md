# Acknowledgements

## RoboEireann

HybridBehaviour is based on many years of experience with coroutine-based
behaviours in the [RoboEireann competition software](https://github.com/roboeireann/RoboEireannCodeRelease).

Notably, the CoroBehaviour implementation in the RoboEireann software uses the
task-as-a-class paradigm, rather than the task-as-a-method paradigm used in
HybridBehaviour.

## CABSL

The HSM component of HybridBehaviour is inspired by [CABSL](https://github.com/bhuman/CABSL)
(C-based Agent Behavior Specification Language) developed by Thomas Röfer and
the B-Human team.

HybridBehaviour is a reimplementation of some CABSL ideas (particularly related
to HSMs), extended with a stackless coroutine system and a behaviour tree
combinator layer. Comments in the source code highlight derived elements.

Note also that the organisation of this code release (comprising the framework
headers, a text-based soccer simulator, and an example behaviour demonstrating
HybridBehaviour concepts in the context of a simplified soccer simulation) is
intentionally modelled after the CABSL release, though the specific simulator
and example behaviour both differ.

Thanks to Thomas Röfer and the B-Human team for developing CABSL.

## Simulation Environment

The soccer simulation environment used in the examples is a new implementation
loosely inspired by [ASCII Soccer](https://www.cs.cmu.edu/~trb/soccer/) by Tucker Balch.
