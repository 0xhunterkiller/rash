# LOWPRI — nice to have, cleanup, and polish

No behavioral or security impact today. Mostly about keeping the code honest, readable, and quiet under sanitizers and warnings.

---

No open items.

**Resolved:**

- L1. Memory leaks in `free_config`: arrays now freed unconditionally (ASan-verified, no leaks with an empty whitelist and unset env vars).
- L2. Dead code in `main`: unused `exitcode` and unreachable `retval = EXIT_SUCCESS;` removed; the Makefile now builds with `-Werror`.
