# HIGHPRI — fix immediately

Confirmed crashes, memory corruption, and exploitable holes. Hole ids (`#1`–`#6`) are stable across all three priority files.

---

No open items.

**Resolved:**

- H1. Non-string whitelist entry → wild pointer: fixed in `ede00ad` (ASan-verified).
- H2. Heap overflow on long `sysuser`: fixed in `9af433a` (ASan-verified).
- H3. A killed child reports success: fixed in `9af433a`.
- H5. Wrong-user denial is never logged: fixed in `9af433a`.
- H6. Uninitialized struct on parse failure: fixed in `9af433a` (ASan-verified).

**Moved to MEDPRI:** H4 → M8, H7 → M9, H8 → M10.
