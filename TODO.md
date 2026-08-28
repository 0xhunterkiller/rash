# rash — Security TODO

Known ways an agent can overrun the current gate, and the walls that close them.
Root cause running through all of it: **rash filters the command *name*, but grants
full, un-isolated, un-parameterized execution of whatever that name resolves to.**
Name-filtering is a speed bump. Real safety is isolation — containing what a command
can *touch*, not policing which commands run.

Ordered as a staircase: small rungs first, each teaching a primitive the big rung needs.

## The staircase (do in this order)

### 1. Resolve the binary yourself, sanitize env  — SMALL
- **Hole (#6):** `execvp` searches `$PATH`, which the agent inherits. Agent prepends a
  malicious `ls` to `$PATH` → whitelisted `ls` runs *their* binary. You check the name,
  not which file the name resolves to.
- **Move:** use absolute paths / `execv` instead of `execvp`; scrub the environment
  before exec.
- **Teaches:** what a child process inherits — the exact question isolation answers.

### 2. Drop privileges before exec  — SMALL–MEDIUM  ← next wall
- **Hole (#3):** the child runs as *you* — your UID, filesystem, network, permissions.
  A whitelisted `curl` exfiltrates; a whitelisted anything acts with your full power.
- **Move:** `setgid`/`setuid` the child down to an unprivileged user before `exec`.
- **Teaches:** how a process changes its own identity — the primitive namespaces build on.

### 3. Resource limits + timeout  — SMALL
- **Hole (#5):** no limits. A permitted interpreter can fork-bomb or spin CPU/memory;
  the parent `wait`s forever.
- **Move:** `setrlimit` on the child; add a timeout on the wait.
- **Teaches:** how the kernel constrains a process — the model isolation extends.

### 4. Isolation — namespaces / seccomp  — BIG
- **Holes (#1, #2):** *unsolvable by filtering.*
  - **#1 Interpreters = total escape.** Any whitelisted program that runs arbitrary
    code bypasses the whole gate: `python -c "..."`, `bash -c "..."`, `find -exec`,
    `awk 'BEGIN{system()}'`, `vim :!sh`. One interpreter on the list = default-deny is theater.
  - **#2 Args to "safe" commands still do damage.** `cat /etc/shadow` reads secrets,
    `cp /etc/passwd /tmp/exfil` moves files, `tee /etc/cron.d/evil` writes anywhere.
    You gate the verb, never the object.
- **Move:** contain what the command can *see and touch* — separate PID / mount /
  network views via namespaces; restrict syscalls via seccomp.
- **Why it's last:** rungs 1–3 put the primitives (identity change, inheritance,
  kernel constraint) under your feet first, so this becomes "same moves, but the child
  gets a separate *view*" instead of a swamp.

## Separate track (not on the staircase)

### Config integrity  — MEDIUM
- **Hole (#4):** whoever writes `rash.conf` owns the machine. If the agent can write
  its own config (via a whitelisted `tee`/`cp`/`python`), it appends `bash` and walks out.
  Self-modifying whitelist = game over.
- **Move:** protect the config — restrict who can write it; consider integrity checking.

---

**The lesson to hold:** #1 and #2 prove name-filtering has a ceiling. You can't filter
your way to safe. That's why isolation was always the real destination — the whitelist
just buys time until the child can't reach anything it shouldn't.