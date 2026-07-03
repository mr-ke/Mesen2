using Mesen.Interop;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Mesen.Config;

public class GenesisConfig : BaseConfig<GenesisConfig>
{
	[Reactive] public ControllerConfig Port1 { get; set; } = new();
	[Reactive] public ControllerConfig Port2 { get; set; } = new();

	[ValidValues(ConsoleRegion.Auto, ConsoleRegion.Ntsc, ConsoleRegion.Pal)]
	[Reactive] public ConsoleRegion Region { get; set; } = ConsoleRegion.Auto;

	[Reactive] public RamState RamPowerOnState { get; set; } = RamState.Random;

	[Reactive] public bool DisableSprites { get; set; } = false;
	[Reactive] public bool DisableBackground { get; set; } = false;
	[Reactive] public bool RemoveSpriteLimit { get; set; } = false;

	[Reactive][MinMax(0, 100)] public UInt32 PsgVolume { get; set; } = 100;
	[Reactive][MinMax(0, 100)] public UInt32 Ym2612Volume { get; set; } = 100;

	[Reactive] public OverscanConfig NtscOverscan { get; set; } = new() { Top = 0, Bottom = 0 };
	[Reactive] public OverscanConfig PalOverscan { get; set; } = new() { Top = 0, Bottom = 0 };

	public void ApplyConfig()
	{
		ConfigManager.Config.Video.ApplyConfig();

		ConfigApi.SetGenesisConfig(new InteropGenesisConfig() {
			Port1 = Port1.ToInterop(),
			Port2 = Port2.ToInterop(),

			Region = Region,
			RamPowerOnState = RamPowerOnState,

			DisableSprites = DisableSprites,
			DisableBackground = DisableBackground,
			RemoveSpriteLimit = RemoveSpriteLimit,

			PsgVolume = PsgVolume,
			Ym2612Volume = Ym2612Volume,

			NtscOverscan = NtscOverscan.ToInterop(),
			PalOverscan = PalOverscan.ToInterop(),
		});
	}

	internal void InitializeDefaults(DefaultKeyMappingType defaultMappings)
	{
		Port1.InitDefaults(defaultMappings, ControllerType.GenesisController);
	}
}

[StructLayout(LayoutKind.Sequential)]
public struct InteropGenesisConfig
{
	public InteropControllerConfig Port1;
	public InteropControllerConfig Port2;

	public ConsoleRegion Region;
	public RamState RamPowerOnState;

	[MarshalAs(UnmanagedType.I1)] public bool DisableSprites;
	[MarshalAs(UnmanagedType.I1)] public bool DisableBackground;
	[MarshalAs(UnmanagedType.I1)] public bool RemoveSpriteLimit;

	public UInt32 PsgVolume;
	public UInt32 Ym2612Volume;

	public InteropOverscanDimensions NtscOverscan;
	public InteropOverscanDimensions PalOverscan;
}
