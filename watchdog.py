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
from litex.soc.interconnect.csr import AutoCSR, CSRStorage, CSRStatus


class WatchdogTimer(Module, AutoCSR):
    """32-bit countdown watchdog timer.

    Attributes
    ----------
    reset_out : Signal
        Directly connect to CPU reset. Asserts when the counter expires.
    """

    def __init__(self):
        # ── CSR Registers ──
        self._load    = CSRStorage(32, name="load",    description="Timeout reload value (clock cycles)")
        self._ctrl    = CSRStorage(1,  name="ctrl",    description="Bit 0: enable watchdog")
        self._feed    = CSRStorage(1,  name="feed",    description="Write to kick (reload counter)")
        self._counter = CSRStatus(32,  name="counter", description="Current countdown value")

        # ── Output: directly drives CPU reset ──
        self.reset_out = Signal()

        # ── Internal counter ──
        counter = Signal(32)

        # Expose counter to CSR readback
        self.comb += self._counter.status.eq(counter)

        self.sync += [
            If(~self._ctrl.storage[0],
                # Disabled: hold at reload value, deassert reset.
                # This also handles the initial state after system reset
                # (ctrl defaults to 0, so watchdog is disabled until
                # firmware explicitly enables it).
                counter.eq(self._load.storage),
                self.reset_out.eq(0),
            ).Elif(self._feed.re,
                # Kick: firmware wrote to the feed register.
                # Reload counter from the load value. The actual data
                # written doesn't matter — only the write strobe (re) counts.
                counter.eq(self._load.storage),
            ).Elif(counter == 0,
                # Expired: assert reset and hold it.
                # Only a system reset or disabling the watchdog clears this.
                self.reset_out.eq(1),
            ).Else(
                # Running: count down.
                counter.eq(counter - 1),
            )
        ]
