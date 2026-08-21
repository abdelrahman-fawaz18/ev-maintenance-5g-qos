# Purpose: Short aliases for environment checks, reproduction modes, audit, and analysis.
# Inputs: NS3_ROOT for check/smoke/paper targets; SWEEP_DIR for audit/analyze targets.
# Outputs: The same simulator and evidence artifacts produced by reproduce.sh and scripts/.
.PHONY: check smoke paper audit analyze

check:
	./reproduce.sh check --ns3-root "$(NS3_ROOT)"

smoke:
	./reproduce.sh smoke --ns3-root "$(NS3_ROOT)"

paper:
	./reproduce.sh paper --ns3-root "$(NS3_ROOT)"

audit:
	./scripts/audit_sweep.py --sweep-dir "$(SWEEP_DIR)"

analyze:
	./reproduce.sh analyze --sweep-dir "$(SWEEP_DIR)"
