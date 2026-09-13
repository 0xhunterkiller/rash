# HIGHPRI — fix immediately

Confirmed crashes, memory corruption, and exploitable holes. Several are reachable from `/etc/rash/rash.toml`, which is not root-owned today, so "only an admin writes the config" is no mitigation. Hole ids (`#1`–`#6`) are stable across all three priority files.

---

## H1. Non-string whitelist entry → wild pointer
**Where:** `src/config.c:30-41`; read at `src/security.c:69`, freed at `src/config.c:132`.

`wl_size` is set to the full array size, but `wl[i]` is only assigned for `TOML_STRING` elements. `whitelist = ["echo", 42, "ls"]` segfaults in `strcmp` and later `free()`s garbage.

**Fix:** keep a separate filled-slot counter, the way the env loop already uses `ec`.

## H2. Heap overflow on long `sysuser`
**Where:** `src/config.c:15-16`.

`malloc(256)` followed by an unbounded `strcpy` of a config-supplied string. A 400-character `sysuser` writes 401 bytes into a 256-byte region (ASan-confirmed). With a user-writable config, this is attacker-reachable memory corruption.

**Fix:** use `strdup()`, like the whitelist loop already does, and drop the magic 256.

## H3. A killed child reports success
**Where:** `src/main.c:73-75`.

`WEXITSTATUS` is only defined when `WIFEXITED`. For a signalled child it yields 0, so `rash bash -c 'kill -9 $$'` exits 0 and logs "exit status 0". `wait()` also reaps any child, not the one forked.

**Fix:** branch on `WIFEXITED`/`WIFSIGNALED`, report `128 + WTERMSIG(status)`, and use `waitpid(pid, &status, 0)`.

## H4. The log is symlink-hijackable
**Where:** `src/main.c:31`, `src/main.c:44`.

Fixed name `/tmp/rash.log` in a world-writable directory, opened with `fopen(..., "a")`, which follows symlinks. Any local user can pre-plant that path to redirect, forge, or read the audit trail. Verified: rash created and wrote through a planted symlink.

**Fix:** log under a root-owned directory and open with `O_NOFOLLOW` via `openat` + `fdopen`.

## H5. Wrong-user denial is never logged
**Where:** `src/main.c:41` vs `src/main.c:44`.

`check_user()` calls `exit(1)` before the log is even opened, so wrong-user attempts leave no record; only unknown-command attempts are logged. For a tool whose job is the audit trail, the most security-relevant denial is exactly the one missing.

**Fix:** open the log before `check_user`, return an error instead of exiting, and log it.

## H6. Uninitialized struct on parse failure
**Where:** `src/config.c:91-106`.

On `!result.ok`, every field except `parse_success`/`parser_feedback` is garbage. It only survives because `src/main.c:37` jumps past `free_config`; retarget that `goto` and it becomes an arbitrary `free()`. The same path also skips `toml_free()`.

**Fix:** `conf rash_config = {0};`, and call `toml_free` on failure too.

## H7. Config integrity (#4)
**Where:** `src/config.c:93`.

Whoever writes `rash.toml` owns the machine: a whitelisted `tee` or `cp` can append `bash`. Today the file is owned by nonexistent UID 644 with group `hunterkiller`, and a hostile config also triggers H1 and H2.

**Fix:** chown it to root; refuse to load unless root-owned and not group/world-writable, checked with `fstat` on the already-opened fd, not the path.

NOT AN ISSUE (H1 AND H2 resolved independantly)

## H8. Env allowlist passes caller-controlled values
**Where:** `src/config.c:62-76`.

The allowlist gates variable *names*, then copies their *values* straight from the caller's environment. A config listing `BASH_ENV`, `PYTHONPATH`, `PERL5LIB`, or `LD_*` hands the caller code execution inside any whitelisted interpreter.

**Fix:** hard-reject loader and startup variables, or have the config set fixed values instead of inheriting them.

NOT AN ISSUE
