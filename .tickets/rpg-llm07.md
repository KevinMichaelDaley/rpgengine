---
id: rpg-llm07
status: open
deps: [rpg-llm03]
links: []
created: 2026-04-25T23:22:00Z
type: task
priority: 2
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, gossip, knowledge, social]
---
# Gossip Propagation Between NPCs

Knowledge facts spread between NPCs via overheard conversations, trade negotiations, and ambient social proximity. Gossip creates emergent information networks where NPCs learn about world events they did not personally witness.

## Current State
The per-NPC knowledge graph (rpg-llm02) supports personal memory and faction-shared subgraphs, but there is no mechanism for dynamic fact transfer between unrelated NPCs. Gossip bridges this gap.

## Requirements

### 1. Gossip Events
Gossip is triggered by observable social interactions within auditory range:
- **Overheard trade**: When two NPCs are in `BARTER_ACTIVE` (rpg-llm06), nearby NPCs within audio range (10m, modified by occlusion) receive a `GOSSIP_TRADE` event containing the items discussed.
- **Overheard combat**: When ATTACK/DEFEND tools are used, nearby NPCs receive a `GOSSIP_COMBAT` event with attacker, target, and location.
- **Intentional gossip**: An NPC can use a `GOSSIP_TELL` tool (max 1 argument: `topic`) to broadcast a chosen fact from its knowledge graph to all friendly NPCs within 5m.

### 2. Gossip Reception and Filtering
When an NPC receives a gossip event:
1. **Range check**: already enforced by audio/social system.
2. **Language check**: `language_similarity >= 0.3` between speaker and listener.
3. **Trust check**: listeners filter gossip based on their relationship to the speaker (faction affinity, past reliability). Untrusted sources apply a certainty penalty.
4. **Deduplication**: if the fact already exists in the listener's graph (same node hash), update `last_heard_at` and boost certainty slightly instead of inserting a duplicate.
5. **Certainty decay**: each hop reduces certainty by a fixed amount (e.g., 15%). Facts below a certainty threshold (20%) are not re-transmitted.

### 3. Knowledge Graph Insertion
Accepted gossip facts are inserted into the listener's personal knowledge graph:
- Node type: `FACT` (or `EVENT` for combat gossip)
- Edge relation: `HEARD_FROM` → speaker entity node
- Embedding: same as original fact (copied from speaker's graph)
- Timestamp: current time
- Certainty: original certainty × hop_decay

The FAISS index for the listener's graph is updated incrementally with the new embedding.

### 4. Gossip Tool (for Intentional Gossip)
```json
{"name": "GOSSIP_TELL", "arguments": {"topic": "forge location"}}
```
**Validation**: `language_similarity >= 0.3` with at least one friendly NPC within 5m.
**Success**: The NPC's top-ranked fact matching `topic` (via KNOWLEDGE_QUERY) is broadcast as a gossip event to all valid listeners.
**Error messages**:
- `"GOSSIP_TELL failed: no friendly listeners within 5m."`
- `"GOSSIP_TELL failed: no matching fact for topic '<topic>'."`
- `"GOSSIP_TELL failed: language barrier."`

### 5. Emergent Behavior
- Rumors spread radially from event sites.
- Faction borders act as soft barriers (low trust = high decay).
- Gossip about player actions influences NPC attitudes without direct player-NPC interaction.
- Trade gossip allows NPCs to learn about supply/demand in distant regions.

## Files to Create
- `include/ferrum/npc/npc_gossip.h` — gossip event types, filter params
- `src/npc/gossip/npc_gossip_listen.c` — receive, filter, deduplicate, insert
- `src/npc/gossip/npc_gossip_broadcast.c` — emit gossip events from trade/combat/intentional tell
- `src/npc/gossip/npc_gossip_decay.c` — certainty decay and cleanup
- `tests/npc/npc_gossip_tests.c` — multi-hop propagation, faction barriers, deduplication

## Acceptance
- [ ] NPC A witnesses a combat; NPC B (within 10m) hears about it via gossip and stores it in graph.
- [ ] NPC C (friend of B, 10m away) hears it from B with lower certainty.
- [ ] NPC D (enemy faction) hears it from B but rejects it due to low trust.
- [ ] Intentional `GOSSIP_TELL` broadcasts a fact matching the topic keyword.
- [ ] Deduplication: hearing the same fact twice does not create two nodes.
- [ ] Certainty decay: after 3 hops, certainty is below re-transmission threshold.
