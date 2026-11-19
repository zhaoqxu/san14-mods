# Battle contribution and geography statistics

`San14HostApi::battlefield` is an optional ABI v1 tail service. Include
`san14_battlefield.h`. Check the host size, service pointer, service size and
every function used before calling. Older hosts can continue running the Mod
with this feature unavailable. No raw game address is exposed by this API.

All operations require `storage.playthrough`, `game.read.officers` and
`game.read.areas`. Mutations and tactic enumeration run on the UI thread.
Capabilities, area and officer queries also accept the roster refresh data
context. Initialize every output `struct_size`; IDs are officers 1–6000,
areas 1–500. Tactic offsets enumerate the officer's sparse recorded tactics,
returning `SAN14_E_NOT_FOUND` at the end.

Call `capabilities` before `set_recording`. Categories are independently
leased by Mod owner and combined across owners. Release with mask zero on
disable; releasing does not erase accumulated values. A counter unavailable
on the active game build must be displayed as unavailable, rather than zero.

The eight officer counters describe the **commander of the acting unit**:
field troop damage, city/gate garrison damage, own unit battle losses, city
durability damage, executed tactics, chain participation, chain initiation and
chain following. Troop damage uses the actual committed subtraction and is
clamped by remaining troops. Losses include wounded soldiers and do not mean
deaths. Unknown sources and allied damage earn no personal enemy-damage credit.
City durability excludes ordinary field structures and respects the engine's
remaining durability floor.

Independent hits are credited to their observed source. For the primary
target, the engine can merge ordinary damage with deferred tactic damage.
The Mod allocates this one actual subtraction between the ordinary component
and tactic participants by their calculated contributions. Integer largest
remainders preserve the total; officer ID breaks ties. An unknown-source
bucket participates in allocation but is not credited to an officer.
Missing/overflowed evidence suppresses personal attribution for the merged
pool. This is a documented accounting rule, not an assertion that the engine
stores a unique actual share for each participant.

A tactic is counted at the validated execution point, once per actor and
actual tactic in its execution group; multiple targets/effects do not add
casts. Chain participation counts once per participating commander per group
with multiple participating commanders. The primary commander is the
initiator; the others are followers. Thus participation equals initiation
plus following. Tactic names resolve from the current game table, including
unique tactics such as 魏武之强, without a separate hardcoded name list.

Region losses are recorded once at the victim's area. Region defeats are
recorded once per committed unit elimination, independently of the two
officers' views. Elimination history stores only the area ID; the global
area-to-city numeric cache is shared across campaigns and is never written
to a sidecar. City/area names are resolved at display time. Existing history
with only a tile ID remains readable. Unknown locations are omitted.

Counters use unsigned 64-bit saturation. Sparse tactic counts and area totals
are updated in the managed session's memory; they do not write a file per hit.
The save lifecycle freezes this memory with the game save, restores it on
load, and rejects stale callbacks. Existing campaigns start recording from
enable time; past battles cannot be reconstructed.

Initial damage/tactic observation is admitted only on the verified Simplified
Chinese CE profile. Other builds keep supported older statistics and geography
while these optional categories remain unavailable. Actual gameplay accuracy,
UI appearance and overhead require in-game acceptance in addition to synthetic
and native hook tests.
