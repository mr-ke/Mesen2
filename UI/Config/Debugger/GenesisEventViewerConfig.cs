using Avalonia.Media;
using Mesen.Interop;
using Mesen.ViewModels;
using ReactiveUI.Fody.Helpers;

namespace Mesen.Config
{
	public class GenesisEventViewerConfig : ViewModelBase
	{
		[Reactive] public EventViewerCategoryCfg Irq { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xC4, 0xF4, 0x7A));
		[Reactive] public EventViewerCategoryCfg Nmi { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xF4, 0xF4, 0x7A));
		[Reactive] public EventViewerCategoryCfg MarkedBreakpoints { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x18, 0x98, 0xE4));

		[Reactive] public EventViewerCategoryCfg VdpPaletteWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xC9, 0x29, 0x29));
		[Reactive] public EventViewerCategoryCfg VdpVramWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xB4, 0x7A, 0xDA));
		[Reactive] public EventViewerCategoryCfg VdpVramRead { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xE2, 0x51, 0xF7));
		[Reactive] public EventViewerCategoryCfg VdpControlPortWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x00, 0x75, 0x97));
		[Reactive] public EventViewerCategoryCfg VdpControlPortRead { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xD1, 0xDD, 0x42));

		[Reactive] public EventViewerCategoryCfg IoWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x18, 0x98, 0xE4));
		[Reactive] public EventViewerCategoryCfg IoRead { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x9F, 0x93, 0xC6));
		[Reactive] public EventViewerCategoryCfg PsgWrite { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xFF, 0x5E, 0x5E));
		[Reactive] public EventViewerCategoryCfg Ym2612Write { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x5E, 0xFF, 0x9E));

		[Reactive] public EventViewerCategoryCfg Z80BusRequest { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0x4A, 0xFE, 0xAC));
		[Reactive] public EventViewerCategoryCfg Z80Reset { get; set; } = new EventViewerCategoryCfg(Color.FromRgb(0xFE, 0xAC, 0x4A));

		[Reactive] public bool ShowPreviousFrameEvents { get; set; } = true;

		public InteropGenesisEventViewerConfig ToInterop()
		{
			return new InteropGenesisEventViewerConfig() {
				Irq = this.Irq,
				Nmi = this.Nmi,
				MarkedBreakpoints = this.MarkedBreakpoints,

				VdpPaletteWrite = this.VdpPaletteWrite,
				VdpVramWrite = this.VdpVramWrite,
				VdpVramRead = this.VdpVramRead,
				VdpControlPortWrite = this.VdpControlPortWrite,
				VdpControlPortRead = this.VdpControlPortRead,

				IoWrite = this.IoWrite,
				IoRead = this.IoRead,
				PsgWrite = this.PsgWrite,
				Ym2612Write = this.Ym2612Write,

				Z80BusRequest = this.Z80BusRequest,
				Z80Reset = this.Z80Reset,

				ShowPreviousFrameEvents = this.ShowPreviousFrameEvents
			};
		}
	}
}
