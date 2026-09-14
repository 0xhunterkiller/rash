# MEDPRI — build/fix for a better, safer experience

Containment and robustness. The root cause behind the containment items: **rash filters the command *name*, but grants full, un-isolated execution of whatever that name resolves to.** M1 → M2 → M3 is a staircase; each rung teaches a primitive the next one needs.

---

## M1. Resolve the binary before exec (#6)
**Where:** `src/main.c:83`.

Already done: the child clears `environ` and sets a fixed `PATH=/usr/bin:/usr/local/bin`, so `$PATH` is no longer caller-controlled. Still open: `execvp` resolves by search, so rash approves a name, not a file.

**Fix:** resolve to an absolute path in the parent, check that path, and `execv` it, so the thing you decided to allow is the thing that runs.

## M2. Resource limits and timeout (#5)
**Where:** `src/main.c:77-102`.

No limits: a permitted interpreter can fork-bomb or burn CPU and memory, and the parent waits forever.

**Fix:** `setrlimit` in the child before exec (`RLIMIT_NPROC`, `RLIMIT_CPU`, `RLIMIT_AS`) and add a timeout on the wait.

**Note:** H3 is fixed, so a child killed on timeout is already reported and logged as `128 + signal`, not as success.

## M3. Isolation via namespaces and seccomp (#1, #2) — BIG
Unsolvable by filtering. **#1:** any interpreter on the list escapes entirely (`python -c`, `bash -c`, `find -exec`, `kubectl` plugins). **#2:** "safe" commands still do damage through arguments (`cat ~/.ssh/id_ed25519`, `cp`, `tee`). You gate the verb, never the object.

**Fix:** contain what the child can see and touch: PID, mount, and network namespaces plus a seccomp syscall filter. Do this after M1 and M2.

## M4. Silent fork failure
**Where:** `src/main.c:103-106`.

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
**Where:** `src/config.h:17`; PATH hardcoded at `src/main.c:82`.

The `path` field is never initialized or written, so it is a live uninitialized pointer waiting for someone to use it. Meanwhile the child's PATH is hardcoded and can't change without a rebuild.

**Fix:** read `path` from `rash.toml` (absolute, root-owned directories only) and pass it to `setpathforchild`, or delete the field.

## M8. Audit log still lives in `/tmp` (was H4)
**Where:** `src/main.c:36`, `src/main.c:46-53`.

`O_NOFOLLOW` now blocks symlink redirects, but the log is owned by the restricted user, who can edit or truncate it. With `fs.protected_regular=1`, another user pre-creating `/tmp/rash.log` makes `open` fail and rash refuses to run. Without `O_CLOEXEC`, children inherit a writable log handle.

**Fix:** log under a root-owned directory and open with `O_APPEND | O_CLOEXEC`.

## M9. Config integrity (#4) (was H7)
**Where:** `src/config.c:95`.

Not exploitable today: `hunterkiller` cannot write `/etc/rash/rash.toml` or `/etc/rash`. But the file is owned by nonexistent UID 644, likely a `chown`/`chmod` mix-up, so whoever later gets that UID controls the whitelist. rash never checks ownership before trusting the config.

**Fix:** `sudo chown root:root` the file; refuse to load unless root-owned and not group/world-writable, via `fstat` on the opened fd.

## M10. Env allowlist passes caller-controlled values (was H8)
**Where:** `src/config.c:64-81`.

The allowlist gates variable *names*, then copies their *values* from the caller's environment. Harmless today: rash grants no extra privilege and the config lists no loader variables. It becomes code execution inside whitelisted interpreters if someone adds `BASH_ENV`, `PYTHONPATH`, `PERL5LIB`, or `LD_*`.

**Fix:** hard-reject loader and startup variables at parse time, or let the config set fixed values instead of inheriting them.

## M11. POSIX is only requested in `main.c`
**Where:** `src/main.c:1`; `setenv` used at `src/security.c:23`, `src/security.c:29`.

Only `main.c` defines `_POSIX_C_SOURCE 200809L`. `security.c` calls `setenv`, which is POSIX rather than ISO C, and builds only because gcc's default GNU mode exposes it. Under `gcc -std=c99 -pedantic` it fails with an implicit declaration of `setenv`. With the macro on every file, all four compile warning-free.

**Fix:** add `-std=c99 -D_POSIX_C_SOURCE=200809L` to the Makefile `OPTIONS` and delete the `#define`.

## M12. `environ = NULL` is not portable
**Where:** `src/main.c:79`.

Dropping `clearenv()` was right; POSIX rejected it. But a null `environ` isn't guaranteed either; the Linux man page only says it "will probably do". POSIX lets you point `environ` at a NULL-terminated array, and a null pointer is not one.

**Fix:** use `static char *empty_env[] = { NULL }; environ = empty_env;`, or better, build an envp array and `execve` it alongside M1.

## M13. Portability nits: `uid_t` format and reserved names
**Where:** `src/main.c:49`, `src/main.c:56`, `src/security.c:51`; `src/config.c:9`, `src/config.c:24`, `src/config.c:52`.

`uid_t`/`gid_t` are printed with `%d`, but POSIX does not say they are `int`. The `_rashconf_*` helpers start with an underscore at file scope, which ISO C reserves for the implementation.

**Fix:** cast IDs to `(long)` and print with `%ld`; rename the helpers without the leading underscore and make them `static`.
