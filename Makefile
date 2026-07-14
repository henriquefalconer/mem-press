# btop-inspired memory-pressure TUI. Just `make` to run.
.PHONY: run
run:
	@command -v python3 >/dev/null 2>&1 || { echo "python3 not found"; exit 1; }
	@python3 mempressure.py
