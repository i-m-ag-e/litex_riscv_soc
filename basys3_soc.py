#!/usr/bin/env python3
import os
import struct
import argparse
from migen import *
from litex.soc.integration.soc_core import soc_core_args, soc_core_argdict
from litex.soc.integration.builder  import Builder, builder_args, builder_argdict
from litex.soc.integration.soc import SoCRegion
from litex_boards.targets.digilent_basys3 import BaseSoC
from litex.soc.cores.uart import UARTPHY, UART

# ── LITE-X STANDARD CORES & PINS ──
from litex.build.generic_platform import *
from litex.soc.cores.spi import SPIMaster
from litespi.modules import S25FL032
from litespi.opcodes import SpiNorFlashOpCodes as Codes
from litex.soc.cores.bitbang import I2CMaster
from litex.soc.cores.gpio import GPIOOut, GPIOIn, GPIOTristate

# ── YOUR CUSTOM CORES ──
from timer1_core import Timer1LiteX
from watchdog    import WatchdogTimer

class CustomBasys3SoC(BaseSoC):
    def __init__(self, **kwargs):
        # ── 1. Firmware Injection ──
        rom_init = []
        firmware_path = "firmware/bootloader.bin"
        if os.path.exists(firmware_path):
            with open(firmware_path, "rb") as f:
                data = f.read()
            data += b'\x00' * ((4 - len(data) % 4) % 4)
            for i in range(0, len(data), 4):
                rom_init.append(struct.unpack("<I", data[i:i+4])[0])
            print(f"SUCCESS: Found demo.bin. Injecting {len(rom_init)} words into ROM.")

        kwargs["cpu_type"] = "picorv32"
        kwargs["cpu_variant"] = "standard"
        kwargs["uart_name"] = "serial"
        kwargs["uart_rx_fifo_rx_we"] = True
        kwargs["integrated_rom_size"]  = 4 * 1024  
        kwargs["integrated_rom_init"] = rom_init

        kwargs["integrated_sram_size"] = 8  * 1024  

        kwargs["integrated_main_ram_size"] = 32 * 1024
        kwargs["with_led_chaser"] = False

        # ── 2. BUILD THE BASE SYSTEM ──
        super().__init__(**kwargs)

        self.add_constant("BIOS_NONE", 1)
        _here = os.path.dirname(os.path.abspath(__file__))

        # ── 3. Attach Custom Hardware (GPIO & Timer) ──
        portb_pads = Signal(8)
        portc_pads = Signal(8)

        self.submodules.portb = GPIOOut(portb_pads)
        self.submodules.portc = GPIOIn(portc_pads)

        btns = Cat([self.platform.request(f"user_btn{i}", 0) for i in ('u', 'd', 'l', 'r')])
        self.submodules.portd = GPIOIn(btns)

        # ── 3b. PWM Timer (timer1.v via AutoCSR) ──
        # Request 4 PWM output pins from PMOD JB bottom row.
        # Basys3 JB bottom: A15 (pin 7), A17 (pin 8), C15 (pin 9), C16 (pin 10)
        _pwm_io = [
            ("pwm", 0,
                Subsignal("ch_a", Pins("A15")),
                Subsignal("ch_b", Pins("A17")),
                Subsignal("ch_c", Pins("C15")),
                Subsignal("ch_d", Pins("C16")),
                IOStandard("LVCMOS33")
            )
        ]
        self.platform.add_extension(_pwm_io)
        self.platform.add_source(os.path.join(_here, "timer1.v"), language="verilog")

        pwm_pads = self.platform.request("pwm")
        self.submodules.timer1 = Timer1LiteX(pads=pwm_pads)

        # Route Timer1 EventManager IRQ to the PicoRV32 interrupt controller.
        # LiteX auto-assigns the IRQ number and generates TIMER1_INTERRUPT in soc.h.
        self.irq.add("timer1", use_loc_if_exists=True)

        # ── 3c. Watchdog Timer (pure Migen, AutoCSR) ──
        self.submodules.watchdog = WatchdogTimer()
        # Watchdog expiry triggers CPU reset (OR'd with system reset)
        self.comb += If(self.watchdog.reset_out, self.cpu.reset.eq(1))
        # self.comb += If(self.watchdog.reset_out, self.crg.rst.eq(1))

        # ── 4. Define External PMOD Pins (PMOD JA/JB) ──
        # Add a new extension for the second UART
        _uart1_io = [
            ("uart1", 0,
                Subsignal("tx", Pins("A14")), # Example: PMOD JB Pin 1 -> RP2040 RX
                Subsignal("rx", Pins("A16")), # Example: PMOD JB Pin 2 -> RP2040 TX
                IOStandard("LVCMOS33")
            )
        ]
        self.platform.add_extension(_uart1_io)

        # ── 5. Inject LiteX SPI, I2C & Secondary UART Cores ──
        _io = [
            ("spi", 0,
                Subsignal("clk",  Pins("J1")),
                Subsignal("cs_n", Pins("L2")),
                Subsignal("mosi", Pins("J2")),
                Subsignal("miso", Pins("G2")),
                IOStandard("LVCMOS33")
            ),
            ("i2c", 0,
                Subsignal("scl", Pins("H1")),
                Subsignal("sda", Pins("K2")),
                IOStandard("LVCMOS33")
            )
        ]
        self.platform.add_extension(_io)

        self.submodules.i2c = I2CMaster(self.platform.request("i2c"))
        
        self.submodules.spi = SPIMaster(
            pads         = self.platform.request("spi"),
            data_width   = 8,
            sys_clk_freq = self.sys_clk_freq,
            spi_clk_freq = 1e6 # 1 MHz
        )
        self.add_spi_flash(
            mode="1x", 
            module=S25FL032(Codes.READ_1_1_1), 
            with_master=True
        )
        
        # Instantiate UART1 PHY and Core
        self.submodules.uart1_phy = UARTPHY(
            self.platform.request("uart1"),
            clk_freq=self.sys_clk_freq,
            baudrate=115200
        )
        self.submodules.uart1 = UART(
            self.uart1_phy,
            tx_fifo_depth=16,
            rx_fifo_depth=16,
            rx_fifo_rx_we=True
        )

        # ──> NEW: ROUTE THE UART1 EVENT MANAGER TO THE IRQ CONTROLLER <──
        self.irq.add("uart1", use_loc_if_exists=True)

        # ── 6. Physical LED/Switch Routing ──
        leds = Cat([self.platform.request("user_led", i) for i in range(8)])
        switches = Cat([self.platform.request("user_sw", i) for i in range(8)])
        self.comb += [
            leds.eq(portb_pads),       
            portc_pads.eq(switches)     
        ]

    def finalize(self, *args, **kwargs):
        super().finalize(*args, **kwargs)
        print("\n=== ATmega328P Clone CSR Address Map ===")
        for name, region in sorted(self.csr_regions.items()):
            print(f"  {name:<32s}  0x{region.origin:08x}")
        print(f"  {'timer1 (AutoCSR)':<32s}  (see CSR map above)")
        print(f"  {'watchdog (AutoCSR)':<32s}  (see CSR map above)")
        print("========================================\n")

def main():
    parser = argparse.ArgumentParser(description="ATmega328P Complete SoC")
    soc_core_args(parser)
    builder_args(parser)
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()

    soc = CustomBasys3SoC(**soc_core_argdict(args))
    soc.add_constant("ROM_BOOT_ADDRESS", 0x00000000)
    soc.add_constant("CPU_RESET_ADDRESS", 0x00000000)
    
    builder_kwargs = builder_argdict(args)
    builder_kwargs["compile_software"] = False
    builder = Builder(soc, **builder_kwargs)
    builder.build(run=args.build)

if __name__ == "__main__":
    main()
