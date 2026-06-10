using Mesen.Interop;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Mesen.Config
{
	public class NdsConfig : BaseConfig<NdsConfig>
	{
		[Reactive] public ConsoleOverrideConfig ConfigOverrides { get; set; } = new();

		[Reactive] public ControllerConfig Controller { get; set; } = new();

		[Reactive] public bool SkipBootScreen { get; set; } = false;
		[Reactive] public bool AllowInvalidInput { get; set; } = false;

		public void ApplyConfig()
		{
			ConfigManager.Config.Video.ApplyConfig();

			ConfigApi.SetNdsConfig(new InteropNdsConfig() {
				Controller = Controller.ToInterop(),

				SkipBootScreen = SkipBootScreen,
				AllowInvalidInput = AllowInvalidInput
			});
		}

		internal void InitializeDefaults(DefaultKeyMappingType defaultMappings)
		{
			Controller.InitDefaults(defaultMappings, ControllerType.NdsController);
		}
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct InteropNdsConfig
	{
		public InteropControllerConfig Controller;

		[MarshalAs(UnmanagedType.I1)] public bool SkipBootScreen;
		[MarshalAs(UnmanagedType.I1)] public bool AllowInvalidInput;
	}
}
