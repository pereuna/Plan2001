# Plan2001 V6: 200 MiB GEFS Anomaly — Code Review

> **HUOM (19.10.2026, katselmus käsitelty):** tämä tiedosto on
> sellaisenaan katselmus commitista `5a74aa3`, ja sen luvut ovat sen
> hetken. Kaikki viisi kohtaa on käsitelty (`docs/status.md`,
> "OpenAI-katselmuksen (V6, 200 MiB) käsittely"): dokumentaation
> selitys on korjattu, `tools/gefslog` luokittelee hännän peitettyihin
> ja peittämättömiin, saavutettavuuslaskenta on myös käyttäjätilan
> `check`issä, ja kahdeksan selittämätöntä lohkoa tarkentui 13:ksi
> (kaikki `LogFree`-varaus-pareja, eli luokiteltuja). Siksi luvut 9 929
> ja 8 alla eivät ole nykytila: tarkennettu mittaus on `docs/status.md`
> ja `docs/gefs-crashleak.md`. Avoinna on vain katselmuksen suositus
> 10–20 kaatumissyklin toistettavasta testistä.

**Reviewed:** 19 October 2026  
**Repository:** [pereuna/Plan2001](https://github.com/pereuna/Plan2001) (`main`, after V6 merge)  
**Key commit:** [`5a74aa3`](https://github.com/pereuna/Plan2001/commit/5a74aa3a616e204344a3584630b5654cd41d83b6)

## Summary

The reported ~200 MiB increase per QEMU crash was initially attributed to permanently lost GEFS blocks after the last durable `LogSync`. Subsequent measurements **disproved that interpretation for the examined crash image**: a 155.27 MiB allocation-log tail was not equivalent to 155.27 MiB of leaked space.

- **9,929 of 9,937 tail allocations (99.9%)** were already in the reconstructed free-space map.
- **687 of 9,937 blocks (6.9%)** had ever been written to disk; unwritten blocks are not, by themselves, proof of free space.
- The user-space `gefs -c` check returned `ok`, but this check does **not** establish that every allocated block is reachable.
- No GEFS recovery-code change was made; avoiding speculative tail reclamation also avoids a potential double-allocation bug.

## Code Review Findings

1. **Misleading explanation in documentation.** `loadarena()` starts with an *empty* free-range AVL tree and `a->used = a->size`, **not** with the entire arena marked free. Log replay reconstructs free ranges: `LogFree` adds them and `LogAlloc` removes them. Tail allocations skipped during replay may therefore already be free if the corresponding blocks were freed earlier.
2. **`tools/gefslog` still labels the log tail a permanent leak.** It measures allocation-log entries after the replay point, not actual unrecoverable allocated blocks. Its messages and documentation should be corrected.
3. **`check` coverage is incomplete.** Kernel `checkleak()` reports unreachable, non-free blocks but its return value does not fail `checkfs()`; user-space `gefs -c` lacks the equivalent full reachability census. An `ok` result alone cannot rule out a leak.
4. **Eight tail blocks remain unexplained.** The difference between 9,937 tail allocations and 9,929 free blocks needs classification (reachable, pending release, or truly orphaned).
5. **The new 20-test `gefslog` suite is insufficient to close the issue.** Its sync-tail regression test validates log parsing and barrier placement, not long-term block reclamation.

## Recommended Verification Before V7

Run a repeatable **10–20 cycle crash/recovery test on the same GEFS image** under the original V6 workload. After each recovery and clean sync, record actual free-space totals, allocated-but-unreachable blocks, and pending frees. Compare these metrics against the same persistent dataset without crashes; investigate the eight residual blocks. Passing criterion: **no cumulative loss of usable space or growth in genuinely orphaned allocations** across cycles.

## Conclusion

**Keep the GEFS recovery algorithm unchanged for now.** The original “200 MiB tail = 200 MiB leak” diagnosis is invalid, but the original V6 test's repeated growth has not yet been conclusively explained by a same-workload, multi-crash reclamation test. Status: **initial diagnosis disproved; regression verification pending**.

*Scope: source and commit review; QEMU tests were not independently rerun.*
