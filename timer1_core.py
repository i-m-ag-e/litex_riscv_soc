# =============================================================================
# timer1_core.py — LiteX AutoCSR wrapper for the timer1 Verilog PWM engine
#
# This class bridges the raw Verilog ports of timer1_minimal.v to the LiteX
# CSR bus. LiteX generates all bus decode logic automatically — the Verilog
# module never sees Wishbone signals.
#
# CSR Register Map (offsets auto-assigned by LiteX):
#   ctrl      [1-bit  RW]  — Master enable (bit 0)
#   top       [16-bit RW]  — PWM period (TOP value)
#   cc_a      [16-bit RW]  — Channel A compare (duty cycle)
#   cc_b      [16-bit RW]  — Channel B compare (duty cycle)
#   cc_c      [16-bit RW]  — Channel C compare (duty cycle)
#   cc_d      [16-bit RW]  — Channel D compare (duty cycle)
#   tcnt      [16-bit RO]  — Counter snapshot (1-cycle latency behind hardware)
#   ev_status [1-bit  RO]  — Raw overflow IRQ state (from Verilog)
#   ev_pending[1-bit  RW]  — Latched overflow event (write 1 to clear)
#   ev_enable [1-bit  RW]  — Overflow interrupt enable
# =============================================================================

from migen import *
from litex.soc.interconnect.csr import AutoCSR, CSRStorage, CSRStatus
from litex.soc.interconnect.csr_eventmanager import EventManager, EventSourcePulse


class Timer1LiteX(Module, AutoCSR):
    """LiteX AutoCSR wrapper for the 16-bit multi-channel PWM timer.

    Parameters
    ----------
    pads : Record or None
        Platform pads with .ch_a, .ch_b, .ch_c, .ch_d subsignals.
        If None, PWM outputs are left unconnected (useful for simulation).
    """

    def __init__(self, pads=None):
        # ── CSR Registers (firmware-accessible) ──
        self._ctrl = CSRStorage(1,  name="ctrl", description="Bit 0: master enable")
        self._top  = CSRStorage(16, name="top",  description="PWM period (TOP value)")
        self._cc_a = CSRStorage(16, name="cc_a", description="Channel A compare value")
        self._cc_b = CSRStorage(16, name="cc_b", description="Channel B compare value")
        self._cc_c = CSRStorage(16, name="cc_c", description="Channel C compare value")
        self._cc_d = CSRStorage(16, name="cc_d", description="Channel D compare value")
        self._tcnt = CSRStatus(16,  name="tcnt", description="Counter snapshot (1-cycle latency)")

        # ── Internal wires for Verilog instance outputs ──
        oc_a   = Signal()
        oc_b   = Signal()
        oc_c   = Signal()
        oc_d   = Signal()
        ov_irq = Signal()

        # ── Verilog Instance ──
        self.specials += Instance("timer1",
            # Parameters
            p_WIDTH   = 16,

            # Clock / Reset
            i_clk     = ClockSignal("sys"),
            i_rst     = ResetSignal("sys"),

            # Configuration inputs (directly from CSR storage registers)
            i_ctrl_en = self._ctrl.storage[0],
            i_top_val = self._top.storage,
            i_cc_a    = self._cc_a.storage,
            i_cc_b    = self._cc_b.storage,
            i_cc_c    = self._cc_c.storage,
            i_cc_d    = self._cc_d.storage,

            # Status outputs (directly to CSR status register)
            o_tcnt_out = self._tcnt.status,

            # PWM pin outputs
            o_oc_a    = oc_a,
            o_oc_b    = oc_b,
            o_oc_c    = oc_c,
            o_oc_d    = oc_d,

            # Interrupt
            o_ov_irq  = ov_irq,
        )

        # ── Route PWM outputs to physical pads ──
        if pads is not None:
            self.comb += [
                pads.ch_a.eq(oc_a),
                pads.ch_b.eq(oc_b),
                pads.ch_c.eq(oc_c),
                pads.ch_d.eq(oc_d),
            ]

        # ── EventManager: overflow interrupt ──
        # Creates ev_status, ev_pending, ev_enable CSRs automatically.
        # EventSourcePulse latches the 1-cycle ov_irq pulse from the Verilog
        # module so firmware can read/clear it via ev_pending.
        self.submodules.ev = EventManager()
        self.ev.overflow = EventSourcePulse(description="Period boundary (counter wrap) event")
        self.ev.finalize()

        # Connect the Verilog ov_irq pulse to the EventManager trigger
        self.comb += self.ev.overflow.trigger.eq(ov_irq)

        # Expose the IRQ line for SoC-level connection
        self.irq = self.ev.irq
