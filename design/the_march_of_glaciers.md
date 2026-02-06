THE MARCH OF GLACIERS
REVISED STANDALONE DESIGN DOCUMENT
(Immersive-Sim–Forward, Physics-First)

==================================
HIGH CONCEPT
==================================

A subterranean immersive-sim survival FPS where the player reclaims hostile underground spaces using light, heat, sound, physics, and brute improvisation. The world is navigated as much by hearing and environmental cues as by sight. Combat is lethal, messy, and optional. Progress is defined by the spaces you can temporarily make survivable, not by stats or crafting trees.

The player competes with a rival scavenger gang and hostile non-human creatures for periodic trash drops from the city above. Over time, the player survives by creating and defending sources of warmth and light — most often fires — and by fortifying spaces with scrap, debris, and even the skulls of slain monsters.

==================================
CORE DESIGN PILLARS
==================================

1. Sensory Navigation Over UI
   - Sound, darkness, heat, and motion are primary information channels.
   - Minimal HUD; players read the world itself.

2. Physics Over Systems
   - If something matters, it exists physically in the world.
   - No crafting trees, no engineering simulations, no abstract modifiers.

3. Immersive-Sim Combat
   - Lethal, contextual, and improvisational.
   - Multiple valid solutions to every encounter.

4. Scarcity Shapes Behavior
   - Limited materials, limited strength, limited time.
   - Players solve problems with what they can physically access.

5. Temporary Safety, Permanent Consequences
   - Safety comes from fragile, interruptible sources of heat and light.
   - The world remembers what you physically change.

==================================
CORE GAME LOOP
==================================

1. Move through cold, dark tunnels using sound and environmental cues.
2. Create or locate a source of heat and light (usually a fire).
3. Use that warmth as a temporary anchor point.
4. Explore outward to scavenge, fight, or avoid enemies.
5. Build barricades and structures to buy time and space.
6. Respond to periodic trash drops and faction conflict.
7. Lose, abandon, or relocate your fire as pressure increases.

The loop is defined by how long you can keep warmth alive.

==================================
WORLD & ENVIRONMENT
==================================

SETTING
- Underground tunnels, maintenance corridors, abandoned infrastructure.
- The city above is present only through trash drops and distant noise.
- Geometry is modular, industrial, and readable at Quake / Half-Life fidelity.

LIGHT
- Darkness reduces visibility and situational awareness.
- Light reveals silhouettes, movement, and space.
- Light sources can be destroyed, blocked, or extinguished.

HEAT
- Cold areas slow the player and enemies.
- Cold affects reload speed, stamina recovery, and breath visibility.
- Heat allows survival, rest, and recovery.

SOUND
- Directional, distance-based, occlusion-aware sound.
- Footsteps, gunfire, machinery, falling debris all propagate.
- Players and enemies can track, mask, and mislead via sound.

==================================
FIRES & TEMPORARY BASES
==================================

FIRES AS BASES
- A base is any persistent source of heat and illumination.
- Most bases are fires.
- Fires:
  - Warm the surrounding area.
  - Provide light.
  - Mark territory.
  - Attract attention.

LIMITED BASES
- The player can usually sustain only one active fire.
- Maintaining multiple fires requires:
  - Long-burning fuel sources.
  - Clever placement.
  - Ongoing risk and effort.
- It is possible, but never trivial.

RESTING
- The player can rest by a fire.
- Resting:
  - Heals the player.
  - Takes time.
  - Reduces awareness.
- Rest can be interrupted by enemies, noise, or environmental changes.

Resting is safe only if the player made it safe.

LOSS & ABANDONMENT
- Fires burn out.
- Fires can be extinguished.
- Fires can be discovered and attacked.
- Losing a fire forces relocation and improvisation.

==================================
FACTIONS & CONFLICT
==================================

RIVAL GANG
- One primary scavenger gang.
- Distinct visual silhouette and sound profile.
- Patrols, scouts, and drop-response teams.

SCRIPTED GANG WAR EVENT
- Once per major cycle, a gang war ignites.
- Triggered at a semi-random time and location.
- Redirects patrols, alters soundscape, opens opportunities.
- Feels systemic but is authored.

NON-HUMAN THREATS
- Own cold, dark, unclaimed spaces.
- Dangerous, unpredictable, territorial.
- Source of skulls and organic building material.

==================================
ECONOMY & VENDORS
==================================

TRASH DROPS
- Periodic events where valuable scrap falls from above.
- Highly contested by player and rival gang.
- Loud, chaotic, time-limited.

VENDORS
- Appear in semi-safe zones.
- Inventories are semi-random.
- Sell:
  - Functional gear
  - Broken gear
  - Useless-looking junk that may be physically useful

The economy is unreliable by design.

==================================
INVENTORY & PHYSICALITY
==================================

- Limited inventory slots.
- Weight and bulk matter.
- Large objects occupy hands and block vision.
- Dropping items is fast and physical.
- No abstract crafting or upgrade systems.

==================================
BUILDING & BARRICADES
==================================

BUILDING PHILOSOPHY
- No build mode.
- No blueprints.
- No snapping.

If you can move it, stack it, wedge it, or jam it, you can build with it.

MATERIALS
- Scrap metal
- Crates
- Shelving
- Pipes
- Furniture
- Monster skulls

LIMITS
- Availability of materials.
- Player strength and stamina.
- Level geometry and space.
- Time pressure and noise.

STRUCTURES
- Barricades
- Chokepoints
- Temporary cover
- Noise traps

All structures are unstable, destructible, and temporary.

==================================
MONSTER SKULLS AS BUILDING MATERIAL
==================================

SKULL PROPERTIES
- Physical objects with mass, shape, and durability.
- Differ by monster type.
- Horns, jaws, and cavities affect use.

USES
- Weighting barricades.
- Wedging doors and debris.
- Anchoring unstable stacks.
- Creating loud noise traps.
- Marking territory.

LIMITS
- Heavy and awkward to carry.
- Limited supply.
- Crack, chip, and degrade under stress.

PSYCHOLOGICAL EFFECT
- Enemies may hesitate, react aggressively, or destroy skulls.
- No explicit fear system; reactions are animation- and behavior-driven.

==================================
COMBAT (IMMERSIVE SIM)
==================================

COMBAT PHILOSOPHY
- Combat is lethal, messy, and contextual.
- Avoidance and improvisation are always valid.
- The player is not balanced against enemies.

WEAPONS
- Firearms: loud, slow to reload, limited ammo.
- Melee: desperate, stamina-driven, positional.
- Improvised: bricks, pipes, explosives, debris.

ENVIRONMENTAL INTERACTION
- Shoot lights to create darkness.
- Collapse unstable structures.
- Push objects to block or crush.
- Break pipes to create steam clouds.
- Set fires to deny space or create warmth.

STEALTH
- Based on line of sight, light, and sound.
- No meters or instant-fail states.
- Failure escalates gradually.

ENEMY BEHAVIOR
- Hear sound.
- See light and motion.
- Push, climb, break, and panic.
- React physically to barricades and debris.

==================================
FAILURE & PERSISTENCE
==================================

- Death or retreat does not fully reset the world.
- Physical changes persist.
- Barricades remain until broken or moved.
- Fires burn out naturally if unattended.
- Stashes can be found by others.

==================================
EXPERIENCE GOAL
==================================

The player should be able to say:

"I built a fire in the dark, dragged scrap and skulls into the tunnel, jammed the doors, and tried to rest — then woke up to footsteps and sparks."

The game is about carving out warmth in a hostile world, knowing it can always be taken away.

