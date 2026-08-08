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

	//Mega CD / Sega CD options (Phase A placeholders; wired into audio in Phase D)
	[Reactive] public string? MegaCdBiosPath { get; set; } = null;
	[Reactive][MinMax(0, 100)] public UInt32 PcmVolume { get; set; } = 100;
	[Reactive][MinMax(0, 100)] public UInt32 CddaVolume { get; set; } = 100;

	[Reactive] public OverscanConfig NtscOverscan { get; set; } = new() { Top = 0, Bottom = 0 };
	[Reactive] public OverscanConfig PalOverscan { get; set; } = new() { Top = 0, Bottom = 0 };

	public void ApplyConfig()
	{
		ConfigManager.Config.Video.ApplyConfig();

		ConfigApi.SetGenesisConfig(new InteropGenesisConfig() {
			Port1 = Port1.ToInterop(),
			Port2 = Port2.ToInterop(),

			Region = Region,
			Model = 0,
			RamPowerOnState = RamPowerOnState,

			DisableSprites = DisableSprites,
			DisableBackground = DisableBackground,
			RemoveSpriteLimit = RemoveSpriteLimit,

			PsgVolume = PsgVolume,
			Ym2612Volume = Ym2612Volume,

			MegaCdBiosPath = MegaCdBiosPath ?? "",
			PcmVolume = PcmVolume,
			CddaVolume = CddaVolume,

			NtscOverscan = NtscOverscan.ToInterop(),
			PalOverscan = PalOverscan.ToInterop(),
		});
	}

	internal void InitializeDefaults(DefaultKeyMappingType defaultMappings)
	{
		//When both keyboard layouts are selected, split them between ports so
		//two players can share one keyboard without key conflicts:
		//  P1 gets ArrowKeys, P2 gets WasdKeys.
		//Gamepad mappings (Xbox/Ps4) are shared by both ports (distinguished by
		//the port parameter — P1 uses Pad1/Joy1, P2 uses Pad2/Joy2).
		DefaultKeyMappingType p1Mappings = defaultMappings;
		DefaultKeyMappingType p2Mappings = defaultMappings;

		if(defaultMappings.HasFlag(DefaultKeyMappingType.WasdKeys) && defaultMappings.HasFlag(DefaultKeyMappingType.ArrowKeys)) {
			p1Mappings &= ~DefaultKeyMappingType.WasdKeys;   //P1: ArrowKeys (no WASD)
			p2Mappings &= ~DefaultKeyMappingType.ArrowKeys;   //P2: WasdKeys (no Arrows)
		}

		Port1.InitDefaults(p1Mappings, ControllerType.GenesisController, 0);
		Port2.InitDefaults(p2Mappings, ControllerType.GenesisController, 1);
	}
}

[StructLayout(LayoutKind.Sequential)]
public struct InteropGenesisConfig
{
	public InteropControllerConfig Port1;
	public InteropControllerConfig Port2;

	public ConsoleRegion Region;
	public UInt32 Model;
	public RamState RamPowerOnState;

	[MarshalAs(UnmanagedType.I1)] public bool DisableSprites;
	[MarshalAs(UnmanagedType.I1)] public bool DisableBackground;
	[MarshalAs(UnmanagedType.I1)] public bool RemoveSpriteLimit;

	public UInt32 PsgVolume;
	public UInt32 Ym2612Volume;

	//Mega CD / Sega CD options — must match C++ GenesisConfig field order
	//(char[512] marshaled as ByValTStr, same pattern as VideoConfig.ShaderPreset)
	[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string MegaCdBiosPath;
	public UInt32 PcmVolume;
	public UInt32 CddaVolume;

	public InteropOverscanDimensions NtscOverscan;
	public InteropOverscanDimensions PalOverscan;
}
