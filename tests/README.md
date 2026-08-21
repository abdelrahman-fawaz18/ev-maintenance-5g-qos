# Regression Tests

| File | Invariants |
|---|---|
| `test_paper_configuration.py` | Seed/load matrix, traffic windows, payload ceiling, policy-controlled difference, radio contract |
| `test_analysis.py` | Bootstrap determinism/bounds and matched-seed effect arithmetic |
| `test_audit_sweep.py` | Complete matched matrix acceptance and packet-loss mismatch rejection |
| `test_source_invariants.py` | SPDX declarations, loss accounting, effective baseline QoS, output schema, public-file exclusions |
| `test_sweep_runner.py` | Deterministic prebuilt-executable discovery and error handling |

Run the suite from the repository root:

```bash
python3 -m unittest discover -s tests -v
```

The C++ integration is verified separately by the `smoke` reproduction mode.
