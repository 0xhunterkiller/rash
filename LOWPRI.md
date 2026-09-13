# LOWPRI — nice to have, cleanup, and polish

No behavioral or security impact today. Mostly about keeping the code honest, readable, and quiet under sanitizers and warnings.

---

## L1. Memory leaks in `free_config` [CLOSED]
**Where:** `src/config.c:131`, `src/config.c:140`.

The `> 0` guards skip freeing the arrays themselves, which were still `malloc`'d. ASan reports 17 bytes leaked with an empty whitelist and an env list whose variables are all unset. rash exits right afterwards, so the practical impact is nil, but it keeps sanitizer runs noisy.

**Fix:** free the arrays unconditionally; `free(NULL)` is safe.

## L2. Dead code in `main` [CLOSED]
**Where:** `src/main.c:76`, `src/main.c:108`.

`int exitcode = 0;` is unused and is the only warning under `-Wall -Wextra`. `retval = EXIT_SUCCESS;` is unreachable, because every branch above it jumps to a cleanup label. Both mislead readers about how the exit code is actually produced.

**Fix:** delete both, and add `-Werror` to the Makefile, which already has `-Wall -Wextra`, so new dead code fails the build.
