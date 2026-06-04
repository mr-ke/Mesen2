using Avalonia.Controls;
using Mesen.Config;
using Mesen.Utilities;
using ReactiveUI;
using ReactiveUI.Fody.Helpers;
using System;
using System.Linq;
using System.Reactive;
using System.Reactive.Linq;

namespace Mesen.ViewModels
{
	public class VideoConfigViewModel : DisposableViewModel
	{
		[ObservableAsProperty] public bool ShowCustomRatio { get; }
		public bool IsWindows { get; }
		public bool IsMacOs { get; }

		public ReactiveCommand<Unit, Unit> ResetPictureSettingsCommand { get; }

		[Reactive] public VideoConfig Config { get; set; }
		[Reactive] public VideoConfig OriginalConfig { get; set; }
		public UInt32[] AvailableRefreshRates { get; } = new UInt32[] { 50, 60, 75, 100, 120, 144, 200, 240, 360 };

		public VideoConfigViewModel()
		{
			Config = ConfigManager.Config.Video;
			OriginalConfig = Config.Clone();

			ResetPictureSettingsCommand = ReactiveCommand.Create(() => ResetPictureSettings());

			AddDisposable(this.WhenAnyValue(_ => _.Config.AspectRatio).Select(_ => _ == VideoAspectRatio.Custom).ToPropertyEx(this, _ => _.ShowCustomRatio));
			// NTSC and LCD Grid settings removed - using librashader for all filtering
			AddDisposable(this.WhenAnyValue(_ => _.Config.UseSoftwareRenderer).Subscribe(softwareRenderer => {
				if(softwareRenderer) {
					//Not supported
					Config.UseExclusiveFullscreen = false;
					Config.VerticalSync = false;
				}
			}));

			//Exclusive fullscreen is only supported on Windows currently
			IsWindows = OperatingSystem.IsWindows();

			//MacOS only supports the software renderer
			IsMacOs = OperatingSystem.IsMacOS();

			if(Design.IsDesignMode) {
				return;
			}

			AddDisposable(ReactiveHelper.RegisterRecursiveObserver(Config, (s, e) => { Config.ApplyConfig(); }));
		}

		private void ResetPictureSettings()
		{
			// NTSC and LCD Grid settings removed - using librashader for all filtering
			Config.Brightness = 0;
			Config.Contrast = 0;
			Config.Hue = 0;
			Config.Saturation = 0;
			Config.ScanlineIntensity = 0;
		}
	}
}
