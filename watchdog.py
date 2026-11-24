# =============================================================================
# watchdog.py — Minimalistic 32-bit Watchdog Timer (pure Migen / LiteX)
#
# Operation:
#   1. Firmware writes timeout value to `load` register.
#   2. Firmware sets `ctrl` bit 0 to enable the watchdog.
#   3. Counter counts down from `load` toward 0 every clock cycle.
#   4. Firmware must periodically write to `feed` to reload the counter.
#   5. If counter reaches 0, `reset_out` asserts → CPU reset.
#
# CSR Register Map (offsets auto-assigned by LiteX):
#   load    [32-bit RW]  — Timeout reload value (in clock cycles)
#   ctrl    [1-bit  RW]  — Bit 0: enable watchdog
#   feed    [1-bit  WO]  — Write any value to reload counter (kick)
#   counter [32-bit RO]  — Current countdown value (debug readback)
# =============================================================================

from migen import *
from litex.soc.interconnect.csr import *

class WatchdogTimer(Module, AutoCSR):
    def __init__(self):
        # Match the CSRs you already have
        self.load    = CSRStorage(32, description="Ticks before reset")
        self.ctrl    = CSRStorage(1,  description="Bit 0: Enable")
        self.feed    = CSRStorage(1,  description="Write to feed the dog")
        self.counter = CSRStatus(32,  description="Current tick value")
        
        # The hardware output
        self.reset_out = Signal()

        # Internal counter register
        count = Signal(32)
        self.comb += self.counter.status.eq(count)
        
        # The synchronous logic (happens once per clock cycle)
        self.sync += [
            self.reset_out.eq(0), # Always default to LOW so we don't trap the CPU
            
            If(self.feed.re,
                # If software writes to the feed register, reload the timer
                count.eq(self.load.storage)
            ).Elif(self.ctrl.storage == 1,
                # If enabled, count down
                If(count == 0,
                    self.reset_out.eq(1),       # FIRE the 1-cycle reset pulse
                    count.eq(self.load.storage) # RELOAD instantly so it doesn't fire again next cycle
                ).Else(
                    count.eq(count - 1)
                )
            )
        ]