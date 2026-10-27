# G-SA1 0.6.0 diagnosis: weapon effects stay visible in character mode ("weapon")

Owner report (0.6.0 test, 2026-10-21, gm_bigcity, sandbox, single player): "Entering character mode with a weapon
keeps the weapons effects visible (Physics Gun light)". Rounds plan 14.1c item 7.

Read-only diagnosis. Nothing in SADC/src, LIB or g-sa1 was edited. The experiments are in
`SADC/build/research/weapon/`.

## 1. Summary

* **Cause.** G-SA1 never puts the player's weapon away.
  * When a character starts, the server only hides the player entity, removes its shadow and noclips it
    (`g-sa1/addon/lua/gsa1/sv_gsa1.lua:41-59`).
  * The client only skips drawing the view model and four HUD elements (`cl_character.lua:472-483`).
  * So the sandbox loadout's default weapon, the Physics Gun, stays deployed as the active weapon.
* **Why the glow shows.** The Physics Gun's glow and beam are drawn by a separate networked entity, `physgun_beam`
  (class `C_PhysBeam`). Hiding the player and the view model does not hide that entity, so its glow sprites stay on
  screen.
* **Other leaks from the same cause.**
  * Tool gun: help panel and ghost entity.
  * Camera: it hides the whole Lua HUD, including G-SA1's own overlay.
  * Gravity Gun: a held object keeps floating along.
  * Flashlight.
  * Weapon switching: number keys, last weapon, the spawn menu and pickups all still work.
  * Multiplayer: other players see the weapon world model.
* **Fix (G-SA1 Lua only).** A real holster plus restore, done the GMod way.
  * Switch to a new empty weapon `gsa1_hands`, so the old weapon's own `Holster` runs. Restore the previous weapon on
    exit.
  * Block weapon switches while the player is a character, in both realms, keyed by a networked flag.
  * Swallow the weapon keys.
  * Hide the world and view models (`DrawWorldModel` / `DrawViewModel`) and the Physics Gun beam (the
    `DrawPhysgunBeam` hook).
  * Turn the flashlight off.
  * Retry the holster and fall back to `SetActiveWeapon` if a weapon refuses.
* **Reproduction.** On a model of GMod's weapon rules, the installed 0.6.0 Lua fails 23 of the 40 checks of a new
  test. The prototype fix passes all 40. The model and fix are in `SADC/build/research/weapon/` (section 4).
* **Owner of the fix.** The G-SA1 agent of the 0.6.0-report fix round, once wave B's g-sa1 agents have released
  `autorun` and `cl_character.lua`. No SADC or LIB work is needed (section 8).

## 2. What 0.6.0 does today

The installed copy (`garrysmod/addons/gsa1`) and `g-sa1/addon` have identical `sv_gsa1.lua`, `cl_character.lua`,
`sh_net.lua` and `cl_render.lua` (SHA-256 compared 2026-10-21).

| Concern | 0.6.0 | Where |
|---|---|---|
| Player model | hidden (`SetNoDraw(true)`, `DrawShadow(false)`), noclipped | `sv_gsa1.lua:49-54` |
| Weapon held | **untouched**: stays the active, deployed weapon; not remembered (`GSA1Prev` has movetype and nodraw only) | `sv_gsa1.lua:49` |
| View model | not drawn (`PreDrawViewModel` returns true) | `cl_character.lua:476-478` |
| Local weapon world model | not drawn (first-person rules: `ShouldDrawLocalPlayer` false, player NoDraw) | `cl_character.lua:472-474` |
| Physics Gun beam / glow (`physgun_beam` entity) | **drawn** | nothing handles it |
| Others' view of the weapon world model | **drawn** at the hidden player, who follows the character (no `DrawWorldModel(false)`) | `sv_gsa1.lua` |
| HUD | crosshair, weapon selection, ammo, secondary ammo hidden | `cl_character.lua:480-483` |
| Weapon's own HUD (`SWEP:DrawHUD`, `SWEP:HUDShouldDraw`) | **active** (tool gun help panel; camera hides all Lua HUD) | GM:HUDShouldDraw, base `cl_init.lua:49-74` |
| Attack keys | cleared (`cmd:ClearButtons()`), so weapons cannot fire | `cl_character.lua:298-314` |
| Weapon keys | only `invprev` / `invnext` swallowed (camera distance); `slot0-9` and `lastinv` pass; the usercmd weapon selection is not cleared by `ClearButtons` | `cl_character.lua:486-493` |
| Flashlight | impulse 100 not cleared; lights from the hidden player's eyes | none |
| Respawn while a character | `PlayerSpawn` deactivates; the client re-sends `gsa1_active` within 2 s (`cl_character.lua:398-401`); the loadout's Physics Gun is never put away | `sv_gsa1.lua:85-91` |

`README.md:650` (owner checklist step 4) says "the player model and weapon hidden". Only the view model was hidden.
`sadc/docs/research/gmod.md:799` had recommended "Hide weapons and the HUD", and G64 removes the weapon
(`g64.md:437`). The implementation hid the view model only.

## 3. Causes, with evidence

### C1. The weapon is never holstered (the root cause)

* `sv_gsa1.lua:41-59` does not touch `ply:GetActiveWeapon()`.
* In sandbox the active weapon is usually the Physics Gun:
  * `player_sandbox.lua:76-80` gives `weapon_physgun` last;
  * it then calls `SwitchToDefaultWeapon()`;
  * that selects `cl_defaultweapon` (`lua/includes/extensions/player.lua:169-177`).
* Everything a deployed weapon does that is not "draw my view model" therefore keeps happening.

### C2. The Physics Gun "light" is the `physgun_beam` entity's glow, not the weapon model

GMod's binaries show that the beam is its own networked entity class (the positions below are file offsets, not RVAs;
corrected after the review, R.2.6):

* `client.dll`, x64 build of 2026-10-15, SHA-256 `28BF1C13005E2FE0...`:
  * `physgun_beam` (0x663E38), `CPhysBeam` (0x663E28);
  * the network table `DT_PhysBeam` (0x6646D8) with `m_hTargetEnt`, `m_HoldPos`, `m_bIsOn`, `m_iPhysBone`;
  * immediately after it, the sprite paths `sprites/physg_glow1` (0x664738), `sprites/physg_glow2` (0x664750) and
    `sprites/physbeam` (0x6647D0);
  * the weapon's `DT_WeaponPhysGun` has `m_hPhysBeam` (0x6658E8);
  * the client ConVar `physgun_drawbeams`: "Whether to draw Physics Gun beams." (0x663E10).
* `server.dll` lists `physgun_beam` next to `viewmodel` and `gmod_hands` in its list of entities kept across map
  cleanup (offset 9098008). It is a server entity like the view model.

So neither `SetNoDraw` on the player nor the `PreDrawViewModel` hook covers it.

Base `cl_init.lua:630-639` documents the `DrawPhysgunBeam(ply, weapon, bOn, target, boneid, pos)` hook as "Return false
to override completely". Sandbox uses it for the held-prop halo (`sandbox/gamemode/cl_init.lua:121-156`).

UNVERIFIED (GMod's C++ is not readable here): whether the glow is drawn at the view model's muzzle (the engine treats
a `ShouldDrawLocalPlayer` = false player as first person) or at the hidden player's hands. Settled by the review (R.1):
`DrawPhysgunBeam` returning false also suppresses the glow sprites - the wiki's GM:DrawPhysgunBeam: "Return false to
hide default effects", it "is also capable of overriding physgun sprite effects, while the convar does not".

The fix does not depend on the location: the holster turns the weapon off, so the beam entity has nothing to draw, and
the client hook hides the beam and the glow on its own. Owner question 1 can settle the location.

Corroborating evidence from the owner's own install (another addon's assertion, not a measurement; R.2.6):

* SkateGM (`addons/skategm/lua/autorun/server/skategm_sv.lua:44-45`) hit the same problem with the same hiding
  pattern. Its comment says a held weapon "(e.g. the physgun's glow) would keep drawing where the hidden player is,
  following the skater around".
* Its fix: `SetActiveWeapon(NULL)` and `DrawWorldModel(false)` (lines 46-55), the switch and pickup gates (164-165),
  and restoring the weapon with `SelectWeapon` (80-83).

### C3. The other weapons' effects are tied to "active weapon"

* **Tool gun.**
  * `SWEP:DrawHUD` draws the tool's help panel (`gmod_tool/cl_init.lua:50-63`, ConVar `gmod_drawhelp`).
  * `TOOL:Think` keeps a ghost model aimed along the owner's eye trace (e.g. `stools/thruster.lua:185-217`,
    `ply:GetEyeTrace()`). The owner's eyes are the hidden player's, so the ghost floats near the character.
  * Holster releases the ghost (`gmod_tool/shared.lua:307-328`, `stool.lua:127`).
* **Camera.**
  * `SWEP:HUDShouldDraw` returns false for every element except the weapon selection and the chat
    (`gmod_camera.lua:208-216`).
  * GM:HUDShouldDraw consults the active weapon (base `cl_init.lua:49-74`). G-SA1's own HUDShouldDraw hook returns
    nil for the other names, so `CHudGMod` is hidden: no Lua HUD at all. That includes G-SA1's debug overlay and,
    later, round 5's ring HUD (`cl_hud.lua`).
  * `SWEP:TranslateFOV` (zoom, `gmod_camera.lua:131-135`) and `AdjustMouseSensitivity` (base `cl_init.lua:418-430`)
    also stay in effect. UNVERIFIED: whether the FOV passed to G-SA1's CalcView is already translated.
* **Gravity Gun.** It picks objects up with a press of attack2 and drops them with the next press. G-SA1 clears all
  buttons, so an object held when the character starts stays held in front of the hidden player, following the
  character, with its hold sound. HL2's `CWeaponPhysCannon::Holster` refuses to holster while it holds something
  (HL2 SDK; UNVERIFIED in GMod).
* **Physics Gun holding a prop.** The cleared attack button releases it, so this is not a problem.

### C4. Nothing stops weapon switching while a character

* `PlayerBindPress` swallows only `invprev` / `invnext` (`cl_character.lua:486-493`).
* Corrected after the review (R.2.1): `slotN` and `lastinv` are most likely already blocked. 0.6.0 hides
  `CHudWeaponSelection` while a character, and the wiki's HUD_Element_List says hiding it "will also disable capability
  to switch weapons for the client" (GMod's own camera keeps it drawn "So we can change weapons"). The model's checks 8
  and 9 do not model that element. The switches that really get through are the server-side ones below. (The fix
  swallows `slotN` / `lastinv` anyway: redundant, harmless.)
* The spawn menu's `gm_giveswep` does `Give` + `SelectWeapon` (`sandbox/gamemode/commands.lua:919-925`).
* Pickups can auto-switch.
* Any of these deploys a weapon, with its effects, during character mode. The weapon held before is not remembered,
  so there is nothing to restore.
* GMod's gate is `GM:PlayerSwitchWeapon(ply, old, new)`: "Return true to prevent the switch"
  (`base/gamemode/player_shd.lua:128-135`). Corrected after the review (R.2.2): the hook is predicted - "in
  singleplayer, it will not be called in the Client realm". In the owner's single player only the server half of the
  gate runs, which is enough; the client half matters in multiplayer.

### C5. Multiplayer

* For other players, the hidden player's active weapon world model is still drawn: Source's
  `C_BaseCombatWeapon::ShouldDraw` checks only the local owner's visibility (UNVERIFIED for GMod).
* GMod's own `drive` module hides it with server-side `ply:DrawWorldModel(false)` (`lua/includes/modules/drive.lua:129-130`,
  restored at `233-234`). G64 and SkateGM do the same.
* Clients also cannot tell that a player is a character: there is no networked flag. G-SA1 multiplayer puppets are
  not done (`sh_net.lua:5-9`).

### C6. Flashlight

* `ClearButtons` does not clear impulses, so the F key toggles the flashlight.
* The flashlight lights from the hidden player's eyes (above the character, in the camera's direction).
* G64 switches it off at spawn (`lua/entities/g64_mario.lua:179`).
* The gate is `GM:PlayerSwitchFlashlight`, "Return true to allow" (`base/gamemode/player.lua:581-587`).

## 4. Reproduction (offline, in GMod's own LuaJIT)

Files are in `SADC/build/research/weapon/`:

* `weapon_world.lua`: a model of GMod's weapon rules, both realms of single player in one LuaJIT state.
  * The client is `g-sa1/tests/lua_mock.lua` (copied).
  * The server files run in their own environment.
  * There is one shared player entity, and client-to-server net messages are delivered at once.
* `weapon_test.lua`: the test.
* `lua_host.exe`: a copy of g-sa1's.
* `v060/lua`: a copy of the installed 0.6.0 addon.
* `fix/lua`: the prototype fix.
* `alt/lua`: the SkateGM/G64 variant, `SetActiveWeapon(NULL)` with no hands.
* `weapon_fix.patch`: `git diff --no-index v060/lua fix/lua`.

Run (the argument order is the one `g-sa1/build.py` uses for its Lua tests):

```
lua_host.exe weapon_test.lua <dir with lua_mock.lua + weapon_world.lua> <addon lua dir> [nobeamhook]
```

What the model encodes, with its evidence. The engine rules are a model because GMod's C++ is not readable.

* **Weapon_Switch.** `SelectWeapon`, a usercmd selection and Give's automatic switch all call the
  `PlayerSwitchWeapon` hook (true = prevent), then `old:Holster(new)` (false = refuse), then `new:Deploy()`.
  * TTT selects its unarmed weapon "in order to abuse the fact that this holsters the weapon"
    (`terrortown/gamemode/weaponry.lua:202-213`).
  * The tool gun's Holster runs on `SelectWeapon` (`gmod_tool/shared.lua:120-125, 320-322`).
* **`SetActiveWeapon`.** No hook, no Holster, no Deploy. G64 calls it (`g64_mario.lua:198-200`) and needs a workaround
  on restore: "Workaround for weapon becoming invisible and unusable", by switching through `weapon_crowbar` first
  (`g64_mario.lua:259-263`).
* **Physics Gun glow.** Two hypotheses are tested:
  * STRICT: drawn while the Physics Gun is deployed;
  * LENIENT: drawn while it is the active weapon.

  In both, a `DrawPhysgunBeam` hook returning false suppresses it.
* **Weapons.** The tool gun, camera and Gravity Gun rules are as in C3. Give is refused by `PlayerCanPickupWeapon`
  (UNVERIFIED; modelled so that the fallback is exercised).

Results (2026-10-21):

| Tree | Checks failed | Notes |
|---|---|---|
| `v060` (installed 0.6.0) | **23 / 40** | See the list below. |
| `fix` | **0 / 40** | |
| `fix`, `nobeamhook` (no client `DrawPhysgunBeam` hook) | 1 / 40 | Only check 7, which tests that hook. The server-side holster alone removes the glow under both hypotheses. |
| `alt` (SkateGM style) | 9 / 40 | Real gaps: no real Holster (5: Physics Gun still "deployed", so STRICT glow is hidden only by the client hook), tool ghost not released (21), Gravity Gun object not dropped (25). Checks 10/12/26/28/30 expect `gsa1_hands` literally; "none" is equivalent there. Check 34 comes from its older restore. |

The checks that fail on `v060`:

* 1-2: the glow, under both hypotheses (active `weapon_physgun`, deployed).
* 4: others see the weapon (`DrawWorldModel` true).
* 5: the weapon is not holstered.
* 6-7: no networked flag, no beam suppression for other characters.
* 8-12: slot keys, `lastinv`, a switch that gets through, client prediction, and the spawn menu all change the active
  weapon during character mode.
* 13: the flashlight switches on.
* 14 and 18: the weapon after exit is the spawn-menu 357, not the Physics Gun. 18 and 19 are knock-on effects of
  earlier steps.
* 20-21: the tool gun's help panel and ghost.
* 23: the camera hides the HUD.
* 25-26: the Gravity Gun keeps its barrel.
* 28: no fallback when a weapon refuses to holster.
* 30: the Physics Gun is out after a respawn while a character.
* 32: no fallback when no weapon can be given.

The same test on the in-progress wave-B tree (`g-sa1/addon/lua`, with the G0 feature autorun) loads cleanly and gives
the same 23 failures. The defect is still present there, and the test is compatible with the new autorun.

`g-sa1/tools/lua_check.py` passes on the three changed files and the new one.

Limits:

* This is a behavioural model. It shows that 0.6.0 never holsters, never gates and never hides, and that the
  prototype does, under the documented Lua semantics.
* The in-game confirmation (section 7.2) is still needed for the C++ parts: where the glow is drawn, the Gravity Gun's
  refusal, and whether `Give` honours `PlayerCanPickupWeapon`.

## 5. GMod API used by the fix

| API | Realm | Status | Evidence |
|---|---|---|---|
| `Player:SelectWeapon(class)`, `HasWeapon`, `GetWeapon`, `GetActiveWeapon`, `Give` | server | verified usage | `extensions/player.lua:173-174`, `sandbox commands.lua:919-925`, beatrun `JumpAnim.lua:1628`, TTT `weaponry.lua:312` |
| `Player:SetActiveWeapon(NULL)` | server | verified usage | G64 `g64_sv_init.lua:250`, `g64_mario.lua:199`; SkateGM `:50`; binding name in `server.dll` |
| `Player:DrawWorldModel(bool)` | server | verified | `drive.lua:130/234` inside `if SERVER`; `server.dll` |
| `Player:DrawViewModel(bool)` | shared | verified | TTT SWEP `Deploy` (server), beatrun `LocalPlayer():DrawViewModel` (client); `server.dll` |
| `PlayerSwitchWeapon` hook (true = prevent) | shared, predicted (single player: server realm only, R.2.2) | verified | `base/gamemode/player_shd.lua:128-135`; SkateGM `:164`; wiki |
| `PlayerSwitchFlashlight` (false = refuse) | server | verified | `base/gamemode/player.lua:581-587` |
| `Player:Flashlight`, `FlashlightIsOn` | server | verified usage | `g64_mario.lua:179` |
| `DrawPhysgunBeam` hook (false = draw nothing) | client | verified, the glow sprites included (wiki, R.1) | `base cl_init.lua:630-639`; name in `lua_shared.dll` |
| `GravGunOnPickedUp` / `GravGunOnDropped` | server | verified | `base/gamemode/gravitygun.lua:20-36` |
| `Entity:ForcePlayerDrop()`, `Player:DropObject()` | server | verified: they release Gravity Gun, Physics Gun and +use holds (wiki, R.1) | `server.dll` names; wiki |
| `weapons.Register(t, class)` | shared | verified | `lua/includes/modules/weapons.lua:46-109` (sets list "Weapon" with `Spawnable`; `DisableDuplicator`) |
| empty `WorldModel = ""`, `ViewModel = c_arms.mdl` | shared | verified usage | GMod's own `lua/weapons/weapon_fists.lua:13-16` |
| unarmed SWEP pattern (`DrawViewModel(false)` in Deploy, no-op `DrawWorldModel`, `ShouldDropOnDie` false) | shared | verified usage | `terrortown/entities/weapons/weapon_ttt_unarmed.lua` |
| `SetNW2Bool` / `GetNW2Bool` | shared | verified usage | beatrun `Infection.lua`, SkateGM `:49`; `server.dll` |
| `Player:SwitchToDefaultWeapon()` | server | verified | `extensions/player.lua:169-177` |
| hook.Call order (first non-nil hook result wins, else the GM function) | shared | verified | `lua/includes/modules/hook.lua:85-141` |
| `Player:Give` honours `PlayerCanPickupWeapon` | server | verified (wiki: "If that hook returns false, this function will do nothing", R.1) | handled by the fallback |
| `weapons.Register` from an autorun-included file before the weapon loader | shared | UNVERIFIED (common practice) | fallback: `addon/lua/weapons/gsa1_hands.lua`, GMod's standard location |

## 6. The fix (exact)

The prototype is `SADC/build/research/weapon/fix/lua`; the diff is `weapon_fix.patch`. It touches 4 files, +186/-15
lines.

### 6.1 New `addon/lua/gsa1/sh_holster.lua` (both realms)

Add it to the `shared` list in `autorun/gsa1_autorun.lua`. That list also sends it to clients.

* `GSA1.HANDS = "gsa1_hands"`, `GSA1.NW_ACTIVE = "gsa1_active"`, and
  `GSA1.IsCharacter(ply)` = `ply:GetNW2Bool(GSA1.NW_ACTIVE, false)`. Round 5's interaction layer can reuse this.
* SWEP `gsa1_hands`, registered with `weapons.Register`:
  * `Spawnable = false`, `AdminOnly`, `DisableDuplicator`;
  * `WorldModel = ""`, `ViewModel = "models/weapons/c_arms.mdl"`, `UseHands = false`;
  * no ammo, no crosshair, `AutoSwitchTo` / `AutoSwitchFrom` false;
  * `Deploy`: server `owner:DrawViewModel(false)`, `self:DrawShadow(false)`;
  * `Holster` returns true;
  * empty attacks and `DrawHUD`, `PreDrawViewModel` returns true;
  * no-op `DrawWorldModel` / `DrawWorldModelTranslucent`;
  * `ShouldDropOnDie` false, `OnDrop` removes it.
* The gate:

```lua
hook.Add("PlayerSwitchWeapon", "gsa1", function(ply, old, new)
    if not GSA1.IsCharacter(ply) then return end
    if IsValid(new) and new:GetClass() == GSA1.HANDS then return end
    return true
end)
```

### 6.2 `addon/lua/gsa1/sv_gsa1.lua`

* **`activate(ply, kind)`** replaces the inline code at `sv_gsa1.lua:47-54`. It:
  * remembers the active weapon's class in `GSA1Prev.weapon`;
  * sets the NW2 flag;
  * keeps the existing noclip, `SetNoDraw` and `DrawShadow` lines;
  * adds `DrawWorldModel(false)` and `DrawViewModel(false)`;
  * turns the flashlight off;
  * calls `holster(ply, false)`.
* **`holster(ply, force)`**:
  * It is done when the hands, or no weapon, are active.
  * It lets go of a Gravity Gun object: the entity tracked through `GravGunOnPickedUp` / `GravGunOnDropped` gets
    `ForcePlayerDrop()`, then `ply:DropObject()` is called.
  * It gives `gsa1_hands` if missing, then calls `SelectWeapon(gsa1_hands)`, a real switch so the old weapon's
    `Holster` runs.
  * With `force`, it calls `SetActiveWeapon(hands or NULL)`: the G64 / SkateGM way, as a last resort.
* **Server `Think` hook `gsa1_holster`.** While a player is active, alive and not holstered, it retries every 0.2 s and
  forces after 1.0 s. This covers:
  * a weapon whose `Holster` refuses;
  * an addon's `SetActiveWeapon`;
  * the loadout after a respawn, re-activated by the client's 2 s re-send.
* **`deactivate`**:
  * clears the flag first, so the gate lets the restore through;
  * calls `DrawWorldModel(true)` and `DrawViewModel(true)` before the restore, so a restored SWEP's `Deploy` can still
    hide its view model;
  * calls `unholster(ply, prev.weapon)`. That selects the previous class if the player still has it, else
    `SwitchToDefaultWeapon()` (only when there was a weapon before), then removes the hands.
* **`PlayerSwitchFlashlight`**: refuses "on" while active.
* **`PlayerSpawn`** (respawn while active): clears the flag, shows the models again and removes the hands. The
  loadout runs after it; the client's re-send re-activates and re-holsters.

### 6.3 `addon/lua/gsa1/cl_character.lua`

* **`PlayerBindPress`**: while `GSA1.Char`, swallow `slot0`-`slot10` and `lastinv`. `invprev` / `invnext` stay the
  camera distance, now swallowed on release too.
* **New `DrawPhysgunBeam` hook**: return false for the local player while `GSA1.Char` (before the server's switch
  arrives), and for any player with the NW2 flag (other characters in multiplayer).

### 6.4 Options

There is no new option: putting the weapon away is part of being the character, so no ConVar and no Q-menu control is
needed. If the owner wants one (owner question 2), it would be a server ConVar `gsa1_sv_holster` (default 1), with a
checkbox on the Options > G-SA1 > Character page.

### 6.5 Optional

* The session log's spawn line (`create_character`, `cl_character.lua:160-164`) could say which weapon was put away,
  for example `weapon put away: weapon_physgun`, so a later log shows it.
* `README.md`:
  * step 4 of the checklist should become "the weapon put away (no Physics Gun glow), given back on remove";
  * add a short section on weapons in character mode.

## 7. Tests

### 7.1 Offline: fails on 0.6.0, passes after the fix

* Add `SADC/build/research/weapon/weapon_test.lua` and `weapon_world.lua` as `g-sa1/tests/weapon_test.lua` and
  `g-sa1/tests/weapon_world.lua`.
  * The test already takes `<tests dir> <addon/lua dir>` like the other Lua tests.
  * It reads `lua_mock.lua` and `weapon_world.lua` from its first argument, so it runs unchanged from `g-sa1/tests`.
* Add `"weapon_test.lua"` to `LUA_TESTS` in `g-sa1/build.py:73`.

Results:

* On 0.6.0: 23 of 40 fail (section 4).
* With the fix: 40 of 40 pass.
* `nobeamhook` mode checks that the server half alone removes the glow.

### 7.2 In game (owner checklist; sandbox, single player)

1. Physics Gun out, then `gsa1_spawn sonic`:
   * no glow anywhere (screen corner, at the character, on walls);
   * number keys and last weapon do nothing;
   * a weapon picked from the Q-menu goes into the inventory only.
2. `gsa1_remove`: the Physics Gun is back in hand and glowing.
3. Tool gun with the thruster tool selected (it has a ghost):
   * spawn: no ghost, no help panel;
   * remove: the tool gun is back.
4. Camera:
   * spawn: the HUD and `gsa1_debug 1` overlay are visible, and the zoom is normal;
   * remove: the camera is back.
5. Gravity Gun holding a barrel:
   * spawn: the barrel drops and the hum stops;
   * remove: the Gravity Gun is back.
6. Flashlight on, then spawn: it is off and F does nothing. After remove, F works.

Console check while a character:

```
lua_run print(Entity(1):GetActiveWeapon())
```

It should print `gsa1_hands`. (The second line first proposed here, a server-side `ents.FindByClass("physgun_beam")`,
cannot show where the client draws the glow; the wiki answers whether the hook hides it: dropped after the review,
R.2.6.)

Multiplayer, when a second client is available: the other player sees no weapon floating where the character is.

## 8. Ownership

* **Owner: the G-SA1 agent of the 0.6.0-report fix round** (the round that consumes `gsa1_diag_060.js`'s topic docs).
  * It changes `addon/lua/autorun/gsa1_autorun.lua` (one list entry), `gsa1/sv_gsa1.lua` and `gsa1/cl_character.lua`.
  * It adds the new `gsa1/sh_holster.lua`, `tests/weapon_test.lua` and `tests/weapon_world.lua`.
  * It patches `build.py` (`LUA_TESTS`) and `README.md`.
  * No SADC, LIB or module (C++) change; no API bump. The addon version gets the release step's bump.
* **When.** After wave B's g-sa1 agents (the G0 seam owns `gsa1_autorun.lua`; the jukebox agents) have finished.
  Release only at a quiescent point, per the release cadence.
  * If it must land while round 5's `5-gsa1` holds the tree, the same code fits the G0 seam as `feat/sh_holster.lua`
    plus `feat/sv_holster.lua` plus `feat/cl_holster.lua`.
  * The server half would then detect the `ply.GSA1Active` transitions in its Think hook, instead of being called from
    `activate` / `deactivate`.
  * The core-file form is preferred: holstering is part of being a character, not an optional feature.
* **Coordination with round 5 (`5-gsa1`).**
  * HOLDABLE props and the attack scan should use `GSA1.IsCharacter(ply)`.
  * The ring HUD (`cl_hud.lua`) depends on this fix when the camera was the active weapon (C3).
  * `DropObject` in `holster` drops only GMod's own +use and Gravity Gun holds, not round 5's server-driven HOLDABLE.
    It runs only while a non-hands weapon is active.

## 9. Owner questions (each has a default; none blocks the fix)

1. Where did the Physics Gun light appear: a glowing ball near the bottom right of the screen that moves with the
   camera, a glow at about head height above the character, or coloured light on the ground or walls? This settles
   the UNVERIFIED part of C2; the fix is the same either way.
2. Should putting the weapon away be switchable? Default: always on, no option.
3. While a character, weapons you walk over or take from the Q menu go into your inventory but are not drawn. Should
   pickups be blocked instead? Default: allow pickups, block the switch.
4. Should the flashlight be usable by the character? Default: off, and F does nothing.

Until the fix ships, the owner can switch to the crowbar before spawning a character (no effects). (Corrected after the
review, R.2.3: `physgun_drawbeams 0` does not help - per the wiki the ConVar does not control the glow sprites.)

## 10. Open / UNVERIFIED

* Where `C_PhysBeam` draws the glow (view model muzzle versus world model). The holster makes this moot. (Whether
  `DrawPhysgunBeam` false suppresses the glow sprites: settled, it does - R.1.)
* GMod's `weapon_physcannon` refusing `Holster` while holding (HL2 SDK behaviour). `ForcePlayerDrop` / `DropObject`
  release a Gravity Gun object (wiki, R.1). The 1 s force fallback covers a refusal.
* (`Player:Give` honours `PlayerCanPickupWeapon`: settled, it does - R.1. If a gamemode refuses `gsa1_hands`, the
  fallback is `SetActiveWeapon(NULL)` plus `DrawWorldModel(false)`.)
* Whether the FOV G-SA1's CalcView receives already includes the camera SWEP's `TranslateFOV`. This becomes moot once
  the camera is holstered.
* Weapons picked up by touch while noclipped and following the character: whether touch pickups fire at all for a
  noclip player moved by `SetPos`.
* Not this topic, but seen: respawn while a character makes the player entity visible at the spawn point for up to
  2 s until the client's re-send (`sv_gsa1.lua:85-91`, `cl_character.lua:398-401`). The holster covers the weapon in
  that window only after the re-send.

## Review (adversarial)

Reviewer, 2026-10-21. Read-only: nothing in SADC/src, LIB or g-sa1 was edited. Experiments are in
`SADC/build/research/weaponreview/`:

* `review_test.lua`: new probes, run as `lua_host review_test.lua ../weapon <addon lua> [defer]`.
* `merged/lua`: the wave-B tree plus this fix.
* `testsmock/`: a copy of g-sa1/tests with `Model` and `weapons` added to its lua_mock.
* `mockcur/`: weapon_test run from a copy of the current g-sa1/tests/lua_mock.lua.

Sources used: the GMod Lua on disk and the strings in the GMod binaries. Facepunch wiki pages (fetched 2026-10-21):

* Player:SetActiveWeapon, Player:SelectWeapon, Player:DrawWorldModel, Player:DrawViewModel, Player:Give,
  Player:DropObject;
* Entity:Remove, Entity:ForcePlayerDrop;
* GM:PlayerSwitchWeapon, GM:DrawPhysgunBeam, GM:HUDShouldDraw, HUD_Element_List.

### R.1 Verified

* **Reproduction re-run.** Results match section 4 exactly:
  * `v060`: 23 / 40 fail;
  * `fix`: 0 / 40;
  * `fix nobeamhook`: 1 / 40 (check 7 only);
  * `alt`: 9 / 40;
  * the live wave-B tree `g-sa1/addon/lua`: 23 / 40.
* The test runs unchanged from a tests-style directory. `g-sa1/tests/lua_mock.lua` is byte-identical to the copy used
  (SHA-256 `B79683C8...`), and the `mockcur/` run gives the same counts.
* `tools/lua_check.py` passes on the four prototype files.
* The installed 0.6.0 and the repo have identical `sv_gsa1.lua`, `cl_character.lua`, `sh_net.lua` and `cl_render.lua`
  (SHA-256).
* **C1 (root cause): confirmed by reading.**
  * `sv_gsa1.lua:41-59` never touches the weapon, and `GSA1Prev` (:49) holds only the movetype and nodraw.
  * `cl_character.lua:472-483` hides only the view model, the local player and four HUD elements.
  * README step 4 (`README.md:649-650`) promises the weapon is hidden.
  * The sandbox loadout and default weapon citations are correct: `player_sandbox.lua:76-80` and
    `extensions/player.lua:169-177`.
* **C2: confirmed, and one UNVERIFIED point is now settled.**
  * The `client.dll` strings are present at the cited positions: `CPhysBeam`, `physgun_beam`, `DT_PhysBeam` with
    `m_hTargetEnt` / `m_HoldPos` / `m_bIsOn` / `m_iPhysBone`, the `physg_glow1` / `physg_glow2` / `physbeam` sprites,
    and `m_hPhysBeam`.
  * The `server.dll` keep list is at 0x8AD318 (= 9098008): `viewmodel`, `physgun_beam`, ..., `gmod_hands`.
  * The wiki's GM:DrawPhysgunBeam page settles whether the hook covers the glow. "Return false to hide default effects".
    The hook "is also capable of overriding physgun sprite effects, while the convar does not" (`physgun_drawbeams`).
  * So the client hook alone hides the glow sprites, and the "glow coverage UNVERIFIED" notes in sections 3, 5 and 10
    can be closed.
  * Where on screen the glow is drawn stays UNVERIFIED (owner question 1). It does not change the fix.
* **Other APIs the doc marked UNVERIFIED, now settled by the wiki.**
  * `Player:Give` "will call GM:PlayerCanPickupWeapon. If that hook returns false, this function will do nothing".
  * `Player:DropObject` and `Entity:ForcePlayerDrop` release Gravity Gun, Physics Gun and +use holds.
  * `Player:DrawWorldModel` = "Show/Hide the player's weapon's worldmodel" (server). Whether the setting survives a
    weapon switch is not documented.
  * `Player:SelectWeapon` "will trigger the weapon switch event". `Player:SetActiveWeapon` "will bypass
    GM:PlayerSwitchWeapon and the currently active weapon's WEAPON:Holster return value".
  * This supports choosing `SelectWeapon` over SkateGM/G64's `SetActiveWeapon(NULL)`.
* **C3: confirmed against the Lua.**
  * Camera: `gmod_camera.lua:131-135` and `:208-216`; base `cl_init.lua:49-74` and `:418-430`.
  * Tool gun: `gmod_tool/cl_init.lua:50-63`, `shared.lua:307-328`, `stool.lua:127`, `thruster.lua:189`.
  * HUD_Element_List: hiding `CHudGMod` "will hide anything rendered with GM:HUDPaint". G-SA1's debug overlay is a
    HUDPaint hook (`cl_character.lua:496`), so the camera does hide it.
* **C6: confirmed by reading.**
  * Impulses are separate from `ClearButtons`.
  * The respawn path is `sv_gsa1.lua:85-91` plus the 2 s re-send at `cl_character.lua:398-401`.
  * The base `PlayerSwitchFlashlight` is at `player.lua:581-587`.
* **Round 5 coordination claim: consistent.** The HOLDABLE design freezes the held prop (`EnableMotion(false)`,
  `libsa1_interaction.md:1050`) and does not use +use pickup. `DropObject` in `holster()` therefore does not touch it.
* **Owner rules: respected.**
  * No game bytes are involved.
  * SA1 has no weapon logic to take from the decompiled code; this is GMod host behaviour.
  * No new option is added. If owner question 2 adds one, it gets a Q-menu checkbox.

### R.2 Refuted or corrected

1. **C4, number keys and last weapon (checks 8 and 9) are most likely model artefacts.**
   * 0.6.0 already hides `CHudWeaponSelection` while a character (`cl_character.lua:480-483`).
   * The wiki's HUD_Element_List says of that element: "Hiding this will also disable capability to switch weapons for
     the client."
   * GMod's own camera relies on the same rule: it keeps that element drawn with the comment "So we can change
     weapons" (`gmod_camera.lua:210-211`).
   * `W.press` in `weapon_world.lua` does not model the HUD element. The `slotN` failures on `v060` are therefore not
     real. `lastinv` probably goes through the same element (UNVERIFIED).
   * The weapon switches that really get through in 0.6.0 are all server side:
     * `gm_giveswep` (`commands.lua:919-925`);
     * the `use <class>` command;
     * Give's automatic switch;
     * other addons.
   * The `PlayerSwitchWeapon` gate is still needed for those. The new `slot0-10` / `lastinv` swallowing is redundant
     but harmless.
2. **The hook is not called on the client in single player.** C4 and section 5 say `PlayerSwitchWeapon` is shared, "so
   the client's predicted switch runs it too". The wiki says: "This hook is predicted. This means that in singleplayer,
   it will **not** be called in the Client realm."
   * In the owner's single-player setup only the server half of the gate runs, which is enough.
   * Check 11 matters only in multiplayer. It tests a property of the fix, not a 0.6.0 defect: 0.6.0 never refuses a
     switch, so it cannot mispredict one.
3. **The section 9 workaround does not hide the glow.** Section 9 says `physgun_drawbeams 0` "probably hides the beam,
   and maybe the glow". Per the wiki, the convar does not control the sprite effects, so it will not remove the light
   the owner reported. Drop that sentence and keep "switch to the crowbar first".
4. **What "23 of 40" means.**
   * It is a count of model checks, not of defects:
     * 8 and 9 are artefacts (item 1);
     * 11 applies only in multiplayer (item 2);
     * 14, 18 and 19 are knock-on effects of 12 and 13.
   * The owner's actual symptom corresponds to checks 1 and 2 only. Both encode a modelled rule (the glow is drawn while
     the gun is active or deployed).
   * So the symptom itself is not reproduced. What the test shows is that 0.6.0 never holsters, which reading the code
     already shows.
   * The test was written together with the fix, so "40 / 40" confirms the prototype against its own model and does not
     validate it in GMod. The in-game checklist in section 7.2 remains the acceptance test.
5. **SkateGM-style variant (`alt`).** "No real Holster" is a modelling assumption. The wiki says only that
   `SetActiveWeapon` bypasses the Holster *return value* and the switch events; whether Holster runs at all is not
   stated. Its failures 5, 21 and 25 are therefore UNVERIFIED, not shown. This does not change the choice of
   `SelectWeapon`, which is the documented switch.
6. **Minor corrections.**
   * The binary positions in C2 are file offsets, not RVAs. Say so.
   * SkateGM's comment (`skategm_sv.lua:44-45`) is another addon's assertion, not a measurement. Call it corroborating
     evidence rather than independent proof.
   * Section 7.2's second console line (a server-side `ents.FindByClass("physgun_beam")`) cannot show where the client
     draws the glow, or whether the hook hides it. The wiki now answers the second question, so the probe can go.

### R.3 Gaps found (reproduced)

1. **Blocker: the fix as written breaks the existing g-sa1 Lua tests.**
   * Setup: `merged/lua` is the wave-B tree plus `sh_holster.lua`, the fix's `sv_gsa1.lua` and `cl_character.lua`
     (identical in wave B), and the one-line autorun change.
   * `log_test` and `warm_test` still pass.
   * `frame_test.lua`, `seam_test.lua` and `feat_jukebox_test.lua` fail at load with
     `gsa1/sh_holster.lua:30: attempt to call global 'Model' (a nil value)`. `g-sa1/tests/lua_mock.lua` has no `Model`
     and no `weapons`; only `weapon_world.lua` supplies them.
   * With `Model` and a `weapons.Register` stub added (`testsmock/`), frame_test and feat_jukebox_test pass. `seam_test`
     still fails 1 of 14, because `seam_test.lua:155` hard-codes the server's include list.
   * `build.py --test` would therefore fail and block the release.
   * Required fix:
     * add `Model` and `weapons` (`Register` / `GetStored`) to `tests/lua_mock.lua`;
     * add `gsa1/sh_holster.lua` to the expected list at `seam_test.lua:155`;
     * run the whole `build.py --test`, not only `lua_check.py`.
2. **Entity:Remove is deferred to the next tick.**
   * The wiki says "The Entity will continue to exist until the start of the next tick" (`IsMarkedForDeletion` exists;
     its name is in both DLLs).
   * When the player switches characters, `GSA1.Spawn` (`cl_character.lua:205-217`) calls `GSA1.Remove(true)`, which
     sends `gsa1_active` false, then sends true.
   * The server handles both in one tick:
     1. `unholster()` removes the hands, but the removal is deferred;
     2. `holster()` still sees `HasWeapon(gsa1_hands)`, skips `Give`, and selects the dying hands;
     3. on the next tick the active weapon becomes NULL.
   * `review_test.lua defer`:
     * R1a (switch Sonic to Knuckles) and R2a (remove and spawn in one tick) fail with the active weapon "none";
     * the old weapon stays holstered and comes back on remove (R1c and R2b pass).
   * Impact is low, but it is the SkateGM NULL state. What the engine does when the active weapon is deleted is
     UNVERIFIED. The switch also deploys and re-holsters the old weapon within one tick.
   * Fix, in `sv_gsa1.lua`:
     * in `holster()`, treat `hands:IsMarkedForDeletion()` as "no hands";
     * or remove the hands with `ply:StripWeapon(GSA1.HANDS)` (the binding is in `server.dll`; whether it detaches
       immediately is UNVERIFIED);
     * better still, `GSA1.Spawn` should not send `false` when it only switches characters. The server already
       handles a repeated `true` (`sv_gsa1.lua:46`).
   * Test: make `Remove` deferred in `weapon_world.lua` (applied at the next `W.frame`), and add R1 and R2 to
     `weapon_test.lua`. Both fail before the hardening and pass after it.
3. **The forced holster leaves the refusing weapon deployed.**
   * In R3, the Gravity Gun refuses and the drop APIs are made ineffective. After 1 s the hands are active, but the
     Gravity Gun is still "deployed" and holding.
   * Since the wiki confirms that `DropObject` and `ForcePlayerDrop` release Gravity Gun holds, this case is
     theoretical. Keep the force path as a last resort; no change is needed.
4. **The client hook alone already fixes the reported symptom.**
   * Per the wiki, the `DrawPhysgunBeam` hook of 6.3 alone (four lines in `cl_character.lua`) removes the owner's
     "Physics Gun light" in single player.
   * If the full holster must wait for wave B to release `autorun` and `cl_character.lua`, the fix round can ship this
     hook first. The holster is still needed for the tool gun, the camera, the Gravity Gun and the flashlight.
5. **Hardening.** Call `DrawWorldModel(true)` after `unholster()` as well. The wiki does not say whether the setting
   survives a weapon switch, and the extra call is harmless.
6. **Naming.** The NW2 name `gsa1_active` equals `GSA1.NET_ACTIVE` (`sh_config.lua:110`). They live in different
   namespaces, so nothing breaks, but greps and logs become confusing. Suggest `gsa1_char`.

### R.4 Required changes before the fix lands (G-SA1 agent of the 0.6.0-report fix round, after wave B)

1. R.3.1: update `tests/lua_mock.lua` and `seam_test.lua:155`. All Lua tests of `build.py --test` must pass on the
   merged tree.
2. R.3.2: harden against the deferred removal, and add the deferred-Remove model plus the switch scenarios to
   `weapon_test.lua`.
3. Correct sections 3 (C4), 5, 9 and 10 per R.2.1-R.2.3 and R.2.6, and the C2 / section 10 UNVERIFIED note per R.1.
   The fix design itself (holster to `gsa1_hands`, server gate, flashlight, client beam hook, restore) stands.
4. Optional: ship the client `DrawPhysgunBeam` hook first (R.3.4), and the `DrawWorldModel` and naming changes
   (R.3.5, R.3.6).

**Verdict.**

* C1, C2, C3 and C6 hold. C5 is plausible but multiplayer-only and not the owner's symptom.
* C4 is half refuted: slot keys and last weapon are already blocked by the hidden `CHudWeaponSelection`; the
  server-side switches remain.
* The proposed fix would remove the symptom. Holstering and the client hook are each sufficient for the glow.
* It is not landable as written: it breaks 3 existing Lua tests, and it has a deferred-removal race on character switch.

## Fix landed (0.6.0-report fix round, agent fx-gsa1-weapon-log, 2026-10-21)

The design above stands, with the review's required changes (R.4):

* **Where.** The shared parts live in `g-sa1/addon/lua/gsa1/sh_net.lua` (already in the autorun's shared list, so the
  autorun and `seam_test.lua`'s include list are unchanged): `GSA1.NW_CHAR = "gsa1_char"` (R.3.6: not `gsa1_active`),
  `GSA1.IsCharacter(ply)` (both realms; round 5 uses it), the `gsa1_hands` SWEP and the `PlayerSwitchWeapon` gate.
  `sv_gsa1.lua`: `activate` / `holster` / `unholster` / `deactivate`, the `gsa1_holster` Think hook, the Gravity Gun
  hooks, `PlayerSwitchFlashlight`, `PlayerSpawn`. `cl_character.lua`: the weapon binds, the `DrawPhysgunBeam` hook, the
  spawn log line's "weapon put away: <class>".
* **R.3.2 (deferred Entity:Remove).** Both hardenings: a character switch sends only "on" with the new kind
  (`GSA1.Remove(silent, switching)`), and the server never removes the hands in the tick the character ends - the
  Think hook removes them a tick later when the player is no longer a character; `holster()` treats hands that are
  marked for deletion as absent. The force timer counts from the last moment the weapon was put away (a weapon an addon
  makes active later gets its own second of retries).
* **R.3.5.** `DrawWorldModel(true)` is called again after the restore.
* **R.3.1.** `tests/lua_mock.lua` has `Model`, `weapons.Register` / `GetStored`, `NULL`, the local player's active
  weapon and NW2 values; frame_test, seam_test, feat_jukebox_test, log_test and warm_test pass.
* **Tests.** `g-sa1/tests/weapon_test.lua` (the model of section 4 embedded, `Entity:Remove` deferred by default):
  55 checks - the 34 of section 4, the R1 switch and R2 remove-and-spawn scenarios of the review, hands removed by
  someone else, an addon's `SetActiveWeapon`, the spawn log line, and the first tick after a pause (pops R.4). Current
  tree: 55 / 55; `nobeamhook`: only check 7 fails; the installed 0.6.0: 32 fail; the review's merged prototype (no
  hardening): R1, R1b, R2a, R2b fail (plus the renamed flag and the new log line). `build.py`'s `LUA_TESTS` does not
  list it yet (not this agent's file).
