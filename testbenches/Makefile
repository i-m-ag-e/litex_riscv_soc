# Makefile for Verilog Testbenches
# Uses Icarus Verilog (iverilog) and vvp

# The source files are in the parent directory
SRC_DIR = ..
TB_DIR  = .

# List of testbenches to run
TESTBENCHES = timer1_tb watchdog_tb

all: $(TESTBENCHES)

timer1_tb: $(TB_DIR)/timer1_tb.v $(SRC_DIR)/timer1.v
	@echo "========================================"
	@echo "Compiling and Running Timer1 Testbench"
	@echo "========================================"
	iverilog -o timer1.vvp -I $(SRC_DIR) $(TB_DIR)/timer1_tb.v $(SRC_DIR)/timer1.v
	vvp timer1.vvp
	@echo ""

watchdog_tb:
	@python3 $(TB_DIR)/watchdog_tb.py
	@echo ""

clean:
	rm -f *.vvp *.vcd

.PHONY: all clean $(TESTBENCHES)
