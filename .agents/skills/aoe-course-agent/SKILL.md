---
name: aoe-course-agent
description: Develop and debug player AI for this C++/Qt course RTS project, including worker scheduling, economy and combat coordination, instruction feedback, framework migration and reproducible evaluation. Use for this repository's strategy tasks and closely related course engines.
---

# Course RTS strategy workflow

Read the task's permitted files and test scope, then `AGENTS.md` at the repository root. For this repository, default strategy edits are limited to `UsrAI.cpp` and `UsrAI.h`; explicit user authorization may define another scope.

## Establish the contract

Read the AI interface, snapshot structs, instruction executor and victory checks before choosing tactics. In this engine, converting the enemy siege workshop triggers victory. Confirm frame duration, costs, prerequisites, SN identity, fixed-point conversions and explicit instruction submission. Do not infer mechanics from another Age of Empires game.

## Diagnose before changing thresholds

- Worker problems: trace gathering, carrying, depositing, construction and cancellation. Role labels alone do not prove a stall. Check target ownership, personal progress and path reachability; protect nearly completed buildings.
- Economy problems: account for age/tech reservations, food and gold income, drop-off distance, farm groups, production queues and population. More workers or farms may worsen congestion.
- Combat problems: inspect stage transitions and competing orders. Separate melee and ranged positions; route reinforcements into the current front. Remember enemies outside vision and count towers through confirmed removals.
- Conversion problems: verify guards, towers, priest health/cooldown, reachable approach and the current committed action. Do not cancel a valid conversion solely because friendly army size falls.

Use stable SN identifiers across frames and actual instruction return IDs for feedback. Coordinate shared budget and one unit's competing intents. Preserve known-good behaviors listed by the user.

## Implement and validate

Explain the root cause or specific hypothesis, make the smallest coherent change, then perform only authorized checks. If a broad redesign is authorized, keep a baseline and verify economy, defense, launch and victory together. A compile-only request does not authorize running a game.

Record build mode, map, rotation, outcome and frames. Keep wall time separate from game frames; do not invent server versions, grades or map coverage. If key evidence is missing, ask a focused question while completing independent analysis.

## Read references only as needed

- Interface and code layout: [architecture](../../../docs/architecture.md).
- Current strategy decisions: [strategy](../../../docs/strategy.md).
- Reproduction and CLI: [build and run](../../../docs/build-and-run.md).
- Failure patterns: [lessons](../../../docs/lessons.md).
- Task framing templates: [prompts](../../../docs/prompts.md).

Resolve these links relative to this skill folder. The skill belongs with this repository; it does not install Qt, grant publishing permission or require optional connectors.
