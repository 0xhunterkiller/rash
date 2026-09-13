# MEDPRI — build/fix for a better, safer experience

Containment and robustness. The root cause behind the containment items: **rash filters the command *name*, but grants full, un-isolated execution of whatever that name resolves to.** M1 → M2 → M3 is a staircase; each rung teaches a primitive the next one needs.

---

## M1. Resolve the binary before exec (#6)
**Where:** `src/main.c:67`.

Already done: the child `clearenv()`s and sets a fixed `PATH=/usr/bin:/usr/local/bin`, so `$PATH` is no longer caller-controlled. Still open: `execvp` resolves by search, so rash approves a name, not a file.

**Fix:** resolve to an absolute path in the parent, check that path, and `execv` it, so the thing you decided to allow is the thing that runs.

## M2. Resource limits and timeout (#5)
**Where:** `src/main.c:61-76`.

No limits: a permitted interpreter can fork-bomb or burn CPU and memory, and the parent waits forever.

**Fix:** `setrlimit` in the child before exec (`RLIMIT_NPROC`, `RLIMIT_CPU`, `RLIMIT_AS`) and add a timeout on the wait.

**Depends on:** HIGHPRI H3. A child killed on timeout is a signalled child, which rash currently logs as exit status 0.

## M3. Isolation via namespaces and seccomp (#1, #2) — BIG
Unsolvable by filtering. **#1:** any interpreter on the list escapes entirely (`python -c`, `bash -c`, `find -exec`, `kubectl` plugins). **#2:** "safe" commands still do damage through arguments (`cat ~/.ssh/id_ed25519`, `cp`, `tee`). You gate the verb, never the object.

**Fix:** contain what the child can see and touch: PID, mount, and network namespaces plus a seccomp syscall filter. Do this after M1 and M2.

## M4. Silent fork failure
**Where:** `src/main.c:79-82`.

When `fork()` fails, rash returns 1 with no message on stderr and no log entry. The caller sees what looks like an ordinary command failure, and the audit trail has a gap exactly when the system is under pressure, such as a fork bomb hitting limits.

**Fix:** `perror("rash")` and `write_log` the failure with `errno` before jumping to cleanup.

## M5. Exit codes conflate rash and the child
**Where:** `src/main.c`, all failure paths.

rash's own `EXIT_FAILURE` is indistinguishable from a child that legitimately exited 1, so callers cannot tell "rash refused" from "the command failed". Scripts and agents wrapping rash will misread denials as ordinary errors.

**Fix:** reserve a range for rash's own failures, in the 126/127 style, and document the codes.

## M6. Real vs effective IDs in `check_user`
**Where:** `src/security.c:54`.

The check compares `getuid()`/`getgid()`. That is correct today, since 07cdea6 backed off setuid, but real and effective IDs diverge the moment setuid comes back, and nothing in the code flags that this check must then be rethought.

**Fix:** write down the privilege model now; if setuid returns, decide which IDs to compare and drop privileges explicitly before exec.

## M7. `conf.path` is never set
**Where:** `src/config.h:17`; PATH hardcoded at `src/main.c:66`.

The `path` field is never initialized or written, so it is a live uninitialized pointer waiting for someone to use it. Meanwhile the child's PATH is hardcoded and can't change without a rebuild.

**Fix:** read `path` from `rash.toml` (absolute, root-owned directories only) and pass it to `setpathforchild`, or delete the field.
