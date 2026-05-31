using Mesen.Config;
using Mesen.Interop;
using ReactiveUI;
using ReactiveUI.Fody.Helpers;
using System;
using System.IO;
using System.Reactive.Linq;

namespace Mesen.ViewModels
{
	public class VideoRecordConfigViewModel : DisposableViewModel
	{
		[Reactive] public string SavePath { get; set; }
		[Reactive] public VideoRecordConfig Config { get; set; }
		
		[ObservableAsProperty] public bool CompressionAvailable { get; set; }
		
		public VideoRecordConfigViewModel()
		{
			Config = ConfigManager.Config.VideoRecord.Clone();

			SavePath = Path.Join(ConfigManager.AviFolder, EmuApi.GetRomInfo().GetRomName() + ".avi");

			AddDisposable(this.WhenAnyValue(x => x.Config.Codec).Select(x => x == VideoCodec.ZMBV).ToPropertyEx(this, x => x.CompressionAvailable));
		}

		public void SaveConfig()
		{
			ConfigManager.Config.VideoRecord = Config.Clone();
		}
   }
}
