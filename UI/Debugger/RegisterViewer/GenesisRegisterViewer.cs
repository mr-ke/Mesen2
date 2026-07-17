using Mesen.Debugger.ViewModels;
using Mesen.Interop;
using System.Collections.Generic;
using static Mesen.Debugger.ViewModels.RegEntry;

namespace Mesen.Debugger.RegisterViewer;

public class GenesisRegisterViewer
{
	public static List<RegisterViewerTab> GetTabs(ref GenesisState state, RomFormat romFormat)
	{
		List<RegisterViewerTab> tabs = new() {
			GetM68KTab(ref state),
			GetZ80Tab(ref state),
			GetVdpTab(ref state),
			GetSystemTab(ref state),
		};
		return tabs;
	}

	private static RegisterViewerTab GetM68KTab(ref GenesisState state)
	{
		List<RegEntry> entries = new List<RegEntry>();

		GenesisM68KState m68k = state.M68K;

		entries.Add(new RegEntry("", "Data Registers"));
		for(int i = 0; i < 8; i++) {
			entries.Add(new RegEntry("", "D" + i, m68k.D[i], Format.X32));
		}

		entries.Add(new RegEntry("", "Address Registers"));
		for(int i = 0; i < 7; i++) {
			entries.Add(new RegEntry("", "A" + i, m68k.A[i], Format.X32));
		}
		entries.Add(new RegEntry("", "A7 (USP)", m68k.A[7], Format.X32));

		entries.AddRange(new List<RegEntry>() {
			new RegEntry("", "Program Counter / Stack"),
			new RegEntry("", "PC", m68k.PC, Format.X32),
			new RegEntry("", "SR", m68k.SR, Format.X16),
			new RegEntry("", "SSP", m68k.SSP, Format.X32),

			new RegEntry("", "SR Flags"),
			new RegEntry("SR.15", "T - Trace", (m68k.SR & 0x8000) != 0),
			new RegEntry("SR.13", "S - Supervisor", (m68k.SR & 0x2000) != 0),
			new RegEntry("SR.10-8", "I - Interrupt mask", (m68k.SR >> 8) & 0x07),
			new RegEntry("SR.4", "X - Extend", (m68k.SR & 0x10) != 0),
			new RegEntry("SR.3", "N - Negative", (m68k.SR & 0x08) != 0),
			new RegEntry("SR.2", "Z - Zero", (m68k.SR & 0x04) != 0),
			new RegEntry("SR.1", "V - Overflow", (m68k.SR & 0x02) != 0),
			new RegEntry("SR.0", "C - Carry", (m68k.SR & 0x01) != 0),

			new RegEntry("", "Status"),
			new RegEntry("", "Stopped", m68k.Stopped),
		});

		return new RegisterViewerTab("M68K", entries, CpuType.GenesisM68K);
	}

	private static RegisterViewerTab GetZ80Tab(ref GenesisState state)
	{
		List<RegEntry> entries = new List<RegEntry>();

		GenesisZ80State z80 = state.Z80;

		entries.AddRange(new List<RegEntry>() {
			new RegEntry("", "Main Registers"),
			new RegEntry("", "A", z80.A, Format.X8),
			new RegEntry("", "Flags", z80.Flags, Format.X8),
			new RegEntry("", "B", z80.B, Format.X8),
			new RegEntry("", "C", z80.C, Format.X8),
			new RegEntry("", "D", z80.D, Format.X8),
			new RegEntry("", "E", z80.E, Format.X8),
			new RegEntry("", "H", z80.H, Format.X8),
			new RegEntry("", "L", z80.L, Format.X8),

			new RegEntry("", "16-bit Registers"),
			new RegEntry("", "IX", z80.IX, Format.X16),
			new RegEntry("", "IY", z80.IY, Format.X16),
			new RegEntry("", "SP", z80.SP, Format.X16),
			new RegEntry("", "PC", z80.PC, Format.X16),

			new RegEntry("", "Interrupts"),
			new RegEntry("", "I", z80.I, Format.X8),
			new RegEntry("", "R", z80.R, Format.X8),

			new RegEntry("", "Flags"),
			new RegEntry("F.7", "S - Sign", (z80.Flags & 0x80) != 0),
			new RegEntry("F.6", "Z - Zero", (z80.Flags & 0x40) != 0),
			new RegEntry("F.5", "F5", (z80.Flags & 0x20) != 0),
			new RegEntry("F.4", "H - Half-carry", (z80.Flags & 0x10) != 0),
			new RegEntry("F.3", "F3", (z80.Flags & 0x08) != 0),
			new RegEntry("F.2", "P/V - Parity/Overflow", (z80.Flags & 0x04) != 0),
			new RegEntry("F.1", "N - Add/Subtract", (z80.Flags & 0x02) != 0),
			new RegEntry("F.0", "C - Carry", (z80.Flags & 0x01) != 0),

			new RegEntry("", "Status"),
			new RegEntry("", "Halted", z80.Halted),
		});

		return new RegisterViewerTab("Z80", entries, CpuType.GenesisZ80);
	}

	private static RegisterViewerTab GetVdpTab(ref GenesisState state)
	{
		List<RegEntry> entries = new List<RegEntry>();

		GenesisVdpState vdp = state.Vdp;

		entries.AddRange(new List<RegEntry>() {
			new RegEntry("", "Counters"),
			new RegEntry("", "VCounter", vdp.VCounter, Format.X16),
			new RegEntry("", "HCounter", vdp.HCounter, Format.X16),

			new RegEntry("", "Status"),
			new RegEntry("", "VBlank", vdp.VBlank),
			new RegEntry("", "HBlank", vdp.HBlank),
			new RegEntry("", "Display enabled", vdp.DisplayEnable),

			new RegEntry("", "Background Color"),
			new RegEntry("$07.0-3", "Color index", vdp.Regs[7] & 0x0F),
			new RegEntry("$07.4-6", "Palette", (vdp.Regs[7] >> 4) & 0x07),

			new RegEntry("", "Registers"),
		});

		string[] regNames = {
			"Mode Register 1",
			"Mode Register 2",
			"Plane A Nametable Address",
			"Window Plane Address",
			"Plane B Nametable Address",
			"Sprite Table Address",
			"Sprite Tile Select",
			"Background Color",
			"Unused",
			"Unused",
			"H Interrupt Register",
			"Mode Register 3",
			"Mode Register 4",
			"H Scroll Data Address",
			"Unused",
			"Auto Increment",
			"Scroll Size",
			"Window H Position",
			"Window V Position",
			"DMA Length Counter (low)",
			"DMA Length Counter (high)",
			"DMA Source Address (low)",
			"DMA Source Address (mid)",
			"DMA Source Address (high)",
		};

		for(int i = 0; i < 24; i++) {
			entries.Add(new RegEntry("$" + i.ToString("X2"), regNames[i], vdp.Regs[i], Format.X8));
		}

		return new RegisterViewerTab("VDP", entries, CpuType.GenesisM68K);
	}

	private static RegisterViewerTab GetSystemTab(ref GenesisState state)
	{
		List<RegEntry> entries = new List<RegEntry>();

		entries.AddRange(new List<RegEntry>() {
			new RegEntry("", "Frame Number", state.FrameCount, Format.X32),
			new RegEntry("", "Region", state.Region == 0 ? "NTSC" : "PAL", state.Region),
		});

		return new RegisterViewerTab("System", entries, CpuType.GenesisM68K);
	}
}
