# rash — TODO

Two kinds of work live here, and they are not the same kind:

- **Correctness** — rash doesn't do what its own code says it does. Confirmed crashes,
  memory corruption, and a gate that reports success when it shouldn't.
- **Containment** — rash does exactly what its code says, and that still isn't enough.
  Root cause running through all of it: **rash filters the command *name*, but grants
  full, un-isolated, un-parameterized execution of whatever that name resolves to.**
  Name-filtering is a speed bump. Real safety is isolation — containing what a command
  can *touch*, not policing which commands run.

Correctness first, because a gate that segfaults on its own config isn't a gate. Then the
staircase: small rungs first, each teaching a primitive the big rung needs.

Hole ids (`#1`–`#6`) are stable across edits; rung numbers are not.

---

## Rung 0. Make the gate not lie — SMALL, do first

All of these are confirmed with an ASan/UBSan build against a local config. Every one is
reachable from `/etc/rash/rash.toml`, which today is **not root-owned** (see Config
integrity) — so "only an admin writes the config" is not currently a mitigation.

### 0a. Non-string whitelist entry → wild pointer — `shell.c:81-87`, deref at `shell.c:230`, `free()` at `shell.c:147`
`wl_size` is set to the full array size, but `wl[i]` is only assigned when the element is
`TOML_STRING`. Any other type leaves the slot uninitialized.

```toml
whitelist = [ "echo", 42, "ls" ]
```
```
SEGV in strcmp, shell.c:230, rdi = 0xbebebebebebebebe   (ASan malloc poison)
```
**Move:** separate output counter, the way the env loop already does it with `ec`. Count
only slots you actually filled.

### 0b. Heap overflow on long `sysuser` — `shell.c:69-70`
`malloc(256)` then an unbounded `strcpy` of a config-supplied string.

```
sysuser = "AAAA…"  (400 chars)
→ heap-buffer-overflow WRITE of size 401 into a 256-byte region
```
**Move:** `strdup()`, like the whitelist loop already does. Drop the magic 256.

### 0c. A killed child reports success — `shell.c:256-259`
`WEXITSTATUS(status)` is only defined when `WIFEXITED(status)`. For a signalled child it
yields 0, so rash's exit code *and the audit log* both claim the command succeeded.

```
$ rash bash -c 'kill -9 $$'
rash exit=0
log: 1788118464 ran bash with exit status 0
```
**Move:** branch on `WIFEXITED` / `WIFSIGNALED`; report `128 + WTERMSIG(status)` when
signalled. Use `waitpid(pid, &status, 0)` — `wait()` reaps any child, not the one forked.

### 0d. The log is symlink-hijackable — `shell.c:216-218`
Fixed name in a world-writable directory, opened with `fopen(..., "a")`, which follows
symlinks. Any local user can pre-plant `/tmp/rash.log` and redirect, forge, or read the
audit trail. Verified: rash created and wrote through a planted symlink.
**Move:** log under a root-owned directory, and open with `O_NOFOLLOW` via
`openat`/`fdopen` rather than `fopen`.

### 0e. The most security-relevant denial is never logged — `shell.c:212` vs `shell.c:218`
`check_user()` `exit(1)`s before the log is even opened. Wrong-user attempts leave no
record; only unknown-command attempts do. For a tool whose whole job is the audit trail,
this is the wrong way round.
**Move:** open the log before `check_user`, and log the denial.

### 0f. Uninitialized struct on parse failure — `shell.c:57-64`
On `!result.ok`, every field except `parse_success`/`parser_feedback` is garbage. It's
survivable *only* because `shell.c:209` jumps to `end:` and skips `free_config`. Retarget
that one `goto` and it's an arbitrary `free()`. The same path also skips `toml_free()`.
**Move:** `conf rash_config = {0};`. Don't rely on a `goto` target for memory safety.

### 0g. Smaller, same sweep
- `free_config`'s `> 0` guards (`shell.c:145`, `shell.c:152`) skip the array itself, which
  was still `malloc`'d. `AddressSanitizer: 17 byte(s) leaked in 3 allocation(s)` with an
  empty whitelist and an env list whose vars are all unset.
- `conf.path` (`shell.c:45`) is never initialized or written; `shell.c:251` hardcodes PATH
  instead. Wire it up or delete the field — right now it's a live uninitialized pointer.
- `fork()` failure (`shell.c:261-263`) returns 1 silently: no message, no log entry.
- Exit-code conflation: rash's own `EXIT_FAILURE` is indistinguishable from a child that
  legitimately exited 1. Callers can't tell "rash refused" from "command failed". Reserve
  a range (126/127 style) for rash's own failures.
- Dead code: unused `exitcode` (`shell.c:243`, the only compiler warning);
  `retval = EXIT_SUCCESS;` (`shell.c:265`) is unreachable — every branch above it jumps.
- `getuid()`/`getgid()` not `geteuid()`/`getegid()` (`shell.c:181`). Correct today, given
  07cdea6 backed off setuid — and silently wrong the moment setuid comes back.

---

## The staircase

### Rung 1. Resolve the binary yourself, sanitize env — SMALL — *partly done*
- **Hole (#6):** `execvp` searches `$PATH`. Check the name, not which file the name
  resolves to, and a prepended malicious `ls` wins.
- **Done:** the child `clearenv()`s and sets a fixed
  `PATH=/usr/bin:/usr/local/bin` before exec (`shell.c:249-251`), so `$PATH` is no longer
  caller-controlled at exec time.
- **Still open:**
  - `execvp` still resolves by search. Resolve to an absolute path in the parent and
    `execv` it, so the thing you decided to allow is the thing that runs.
  - The env allowlist passes through caller-supplied *values* (`shell.c:100-111`). A
    config listing `BASH_ENV`, `PYTHONPATH`, `PERL5LIB`, `LD_*` hands the caller code
    execution inside any whitelisted interpreter — the allowlist gates names, not
    content. Same ceiling as #1, one layer down.
- **Teaches:** what a child process inherits — the exact question isolation answers.

### Rung 2. Resource limits + timeout — SMALL
- **Hole (#5):** no limits. A permitted interpreter can fork-bomb or spin CPU/memory; the
  parent `wait`s forever.
- **Move:** `setrlimit` on the child; add a timeout on the wait.
- **Note:** the timeout needs 0c fixed first — a child you kill on timeout is a signalled
  child, and today rash would log that as exit status 0.
- **Teaches:** how the kernel constrains a process — the model isolation extends.

### Rung 3. Isolation — namespaces / seccomp — BIG
- **Holes (#1, #2):** *unsolvable by filtering.*
  - **#1 Interpreters = total escape.** Any whitelisted program that runs arbitrary code
    bypasses the whole gate: `python -c "..."`, `bash -c "..."`, `find -exec`,
    `awk 'BEGIN{system()}'`, `vim :!sh`. One interpreter on the list = default-deny is
    theater. (The live `/etc/rash/rash.toml` whitelists `kubectl` — same class.)
  - **#2 Args to "safe" commands still do damage.** `cat /etc/shadow` reads secrets,
    `cp /etc/passwd /tmp/exfil` moves files, `tee /etc/cron.d/evil` writes anywhere.
    You gate the verb, never the object.
- **Move:** contain what the command can *see and touch* — separate PID / mount / network
  views via namespaces; restrict syscalls via seccomp.
- **Why it's last:** rungs 0–2 put the primitives (memory safety, inheritance, kernel
  constraint) under your feet first, so this becomes "same moves, but the child gets a
  separate *view*" instead of a swamp.

---

## Separate track (not on the staircase)

### Config integrity — MEDIUM — *currently broken on this box*
- **Hole (#4):** whoever writes `rash.toml` owns the machine. If the agent can write its
  own config (via a whitelisted `tee`/`cp`/`python`), it appends `bash` and walks out.
  Self-modifying whitelist = game over.
- **Now also:** rung 0 makes this worse than "they add a command" — a hostile config is a
  heap overflow (0b) and a wild `free()` (0a).
- **Observed:** `/etc/rash/rash.toml` is owned by UID **644** — a user that doesn't exist —
  with group `hunterkiller`. A stale root-owned `.rash.toml.swp` sits beside it. The file
  rash stakes everything on is not root-owned.
- **Move:** fix the ownership; refuse to load a config that isn't root-owned and
  non-group/world-writable — `fstat` the fd you already opened, not the path. Then
  consider integrity checking.

---

**The lesson to hold:** #1 and #2 prove name-filtering has a ceiling. You can't filter
your way to safe. That's why isolation was always the real destination — the whitelist
just buys time until the child can't reach anything it shouldn't. Rung 0 is the separate
promise: until it's done, the gate doesn't even hold the line it claims to.
