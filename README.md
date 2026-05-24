
## Exercise 1 — Parallel player threads

### Goal
Create **16 threads in parallel** (8 P-F + 4 P-SM + 4 P-NFS). Each thread prints a "waiting to play" line, sleeps 1–3 s, then prints a "done playing" line. No synchronisation between threads.

### My twist
Instead of writing three independent `for` loops with three nearly-identical bodies, I describe the world as a **small static table** of player groups:

```c
group_t groups[] = {
	{ "P-F",   8, football_player    },
	{ "P-SM",  4, supermario_player  },
	{ "P-NFS", 4, nfs_player         },
};
```

A single generic loop walks this table and calls `pthread_create()` 16 times. The benefits are concrete:

* Adding a 4th game later is **one extra row** — the creation code never changes.
* The "all threads created before any `pthread_join()`" rule is enforced structurally: phase 1 is a creation pass, phase 2 is a joining pass, they can never interleave.
* The three required function signatures (`football_player`, `supermario_player`, `nfs_player`) are kept, but they all delegate to a single `play()` helper so the body lives in one place.

### Thread-safe random numbers
`rand()` shares a global state and is **not** thread-safe. Each player carries its own `seed` field and uses `rand_r(&seed)`, which is per-thread and avoids contention on a hidden global.

---

## Exercise 2 — Multi-game synchronisation

### Constraints recap

| Game | Threads | Max concurrent players |
|------|---------|------------------------|
| Football  (P-F)   | 8 | 4 |
| SuperMario (P-SM) | 4 | 2 |
| NFS        (P-NFS)| 4 | 1 |

Rules:

1. The console hosts **at most one game at a time**.
2. Several players can share the console **only if they play the same game**.
3. Per-game capacity must be respected.
4. **No starvation** — every waiting player must eventually play.

### Why the "obvious" solution is wrong

A first instinct is *"one counting semaphore per game, initialised to its capacity"*. This is wrong: nothing in such a design **couples the three games together**, so an SM player can step on the console while F players are still playing. Rule 1 is violated.

The three constraints together really mean: *the access state of the console is a single shared invariant*. So the design must have a **central piece of state** that all three groups consult.

### My design — central controller + per-game waiting gates

#### Shared variables

| Variable | Role |
|---|---|
| `current_game` | which game is on the console right now (`G_NONE` if free) |
| `active_count` | how many players are currently on the console (always ≤ capacity of `current_game`) |
| `waiting[3]`   | how many players of each game are blocked in their waiting room |
| `last_served`  | the last game we activated — used by the round-robin fairness policy |

#### Semaphores

| Semaphore | Init value | Role |
|---|---|---|
| `mutex`     | `1` | binary semaphore — protects ALL the shared variables above; behaves like a monitor lock |
| `gate[G_F]` | `0` | waiting room for Football players |
| `gate[G_SM]`| `0` | waiting room for Super Mario players |
| `gate[G_NFS]`| `0` | waiting room for Need for Speed players |

The `gate[]` semaphores are **never** initialised to game capacities. They are initialised to `0` and used purely as "hand-off" signals: a leaving (or session-starting) player explicitly posts a seat to ONE waiter.

#### enter_X_game(id) — the protocol

```
lock(mutex)
if console is free  OR  (it already runs my game AND there is room AND no other game has waiters):
	take a seat (active_count++, current_game := my game)
	unlock(mutex) and play
else:
	waiting[my_game]++
	unlock(mutex)
	wait on gate[my_game]            <-- blocked here
	(when released, the releaser already did waiting--/active++ for me)
```

The crucial detail in the fast path is the test **"no other game has waiters"**. Without it, a never-ending stream of fresh F players could keep entering while a few P-SM players are queued — that would be starvation. With it, as soon as anyone is queued for another game, new arrivals of the active game must queue too. The currently-playing batch then drains and the controller switches games.

#### quit_X_game(id) — the protocol

```
lock(mutex)
active_count--
if active_count == 0:
	pick next game in round-robin order starting AFTER last_served
	if some game has waiters:
		start_session(next_game)    # wakes up min(capacity, waiters) players
	else:
		current_game := none
else if no OTHER game has waiters AND my game has waiters:
	release ONE more waiter of my game  (refills the empty seat)
unlock(mutex)
```

#### Why this is starvation-free

Three independent mechanisms cooperate:

1. **Arrivals defer to queues.** New players never overtake players that are already waiting for a different game (the `other_waiting == 0` check in `enter_game`).
2. **Session draining.** Once any other game has even one waiter, the currently active session cannot refill empty seats — it drains down to 0.
3. **Round-robin handover.** When the console becomes empty, `pick_next_game()` scans the games in a rotating order starting just AFTER the last served one. A game cannot be picked twice in a row while another game has waiters.

Together these guarantee that any player who has joined `waiting[g]` will be picked in at most one full round-robin cycle.

#### Six required functions

The subject explicitly requires six procedures: `enter_F_game`, `quit_F_game`, `enter_SM_game`, `quit_SM_game`, `enter_NFS_game`, `quit_NFS_game`. They are present and act as thin wrappers around a generic `enter_game(g, id)` / `quit_game(g, id)`. This avoids three triplicated copies of the same code while still matching the required API.

### Sample trace (abridged)

```
[P-F   #0] ENTERS  Football    (players now on console: 1/4)
[P-F   #1] ENTERS  Football    (players now on console: 2/4)
[P-SM  #0] waits   SuperMario  (queue F=0 SM=1 NFS=0, on=Football/2)
[P-F   #2] ENTERS  Football    (players now on console: 3/4)
[P-F   #3] waits   Football    (queue F=1 SM=1 NFS=0, on=Football/3)   <-- F yields to SM
[P-F   #0] playing Football    (2 s)
[P-F   #0] LEAVES  Football    (players still on console: 2)
... 
[P-F   #2] LEAVES  Football    (players still on console: 0)
		   >>> switching console to SuperMario
[P-SM  #0] ENTERS  SuperMario  (players now on console: 1/2)
```

Notice on line 5 that `P-F #3` is forced to wait even though Football still has a free seat — because `P-SM #0` is already queued. That single line is the visible proof that the anti-starvation rule is alive.

---


