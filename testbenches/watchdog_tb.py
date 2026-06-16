import sys
import os

# Add parent directory to path so we can import the watchdog module
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from migen import *
from watchdog import WatchdogTimer

def watchdog_testbench(dut):
    print("========================================")
    print("   Watchdog Timer (Migen) Testbench")
    print("========================================")
    
    print("--- Test 1: Initial State (Disabled) ---")
    yield
    assert (yield dut.reset_out) == 0, "reset_out should be 0 initially"
    print("[PASS] reset_out is 0")

    print("\n--- Test 2: Enable and Countdown ---")
    # Load 5 into the timeout register
    yield dut._load.storage.eq(5)
    yield
    
    # Enable watchdog
    yield dut._ctrl.storage.eq(1)
    yield
    
    # Wait 2 cycles
    yield
    yield
    # Counter should have decremented: 5 -> 4 -> 3
    val = (yield dut._counter.status)
    assert val == 3, f"Counter should be 3, got {val}"
    assert (yield dut.reset_out) == 0, "Reset should not trigger yet"
    print("[PASS] Watchdog is counting down normally")

    print("\n--- Test 3: Kick (Feed) the Watchdog ---")
    # Pulse the feed read-enable (write strobe)
    yield dut._feed.re.eq(1)
    yield
    yield dut._feed.re.eq(0)
    yield
    
    # Counter should be reloaded to 5 and start decrementing again
    val = (yield dut._counter.status)
    assert val == 5 or val == 4, "Counter should be reloaded"
    print("[PASS] Watchdog kick successfully reloaded counter")

    print("\n--- Test 4: Allow Expiration ---")
    # Wait enough cycles for it to count down to 0
    for _ in range(6):
        yield
    
    # Check that reset_out asserted
    assert (yield dut.reset_out) == 1, "reset_out failed to assert upon reaching 0"
    print("[PASS] reset_out asserted HIGH upon expiration")

    print("\n--- Test 5: Disable Clears Reset ---")
    # Disabling the watchdog should immediately deassert the CPU reset
    yield dut._ctrl.storage.eq(0)
    yield
    yield
    assert (yield dut.reset_out) == 0, "reset_out did not clear when disabled"
    print("[PASS] Watchdog disabled and reset_out cleared successfully")
    
    print("\n========================================")
    print("ALL MIGEN TESTS PASSED")
    print("========================================")

if __name__ == "__main__":
    dut = WatchdogTimer()
    run_simulation(dut, watchdog_testbench(dut), vcd_name="watchdog.vcd")
