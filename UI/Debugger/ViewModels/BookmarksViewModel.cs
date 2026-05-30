using Mesen.Debugger.Utilities;
using Mesen.Debugger.Windows;
using Mesen.Interop;
using Mesen.Utilities;
using Mesen.ViewModels;
using ReactiveUI;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace Mesen.Debugger.ViewModels
{
	public enum BookmarkState
	{
		None,
		Saved,
		Loaded
	}

	public class BookmarksViewModel : DisposableViewModel
	{
		[Reactive] public MesenList<BookmarkViewModel> Bookmarks { get; private set; } = new();

		public CpuType CpuType { get; }
		public TASEditorWindowViewModel TASEditor { get; }

		[Obsolete("For designer only")]
		public BookmarksViewModel() : this(CpuType.Snes, new()) { }

		public BookmarksViewModel(CpuType cpuType, TASEditorWindowViewModel tasEditor)
		{
			CpuType = cpuType;
			TASEditor = tasEditor;

			List<BookmarkViewModel> bookmarks = new List<BookmarkViewModel>();
			for(int i = 1; i <= 10; i++) {
				bookmarks.Add(new BookmarkViewModel(i));
			}
			Bookmarks.Replace(bookmarks);
		}

		public void SaveBookmark(int slotNumber)
		{
			if(slotNumber < 1 || slotNumber > 10) {
				return;
			}

			try {
				TimingInfo timing = EmuApi.GetTimingInfo(CpuType);
				int frameNumber = (int)timing.FrameCount;
				double fps = timing.Fps;
				double totalSeconds = frameNumber / fps;
				int minutes = (int)(totalSeconds / 60);
				int seconds = (int)(totalSeconds % 60);
				int milliseconds = (int)((totalSeconds - (int)totalSeconds) * 1000);
				string timeStamp = $"{minutes:D2}:{seconds:D2}.{milliseconds:D3}";

				EmuApi.SaveState((uint)(slotNumber - 1));

				int index = slotNumber - 1;
				Bookmarks[index].FrameNumber = frameNumber;
				Bookmarks[index].TimeStamp = timeStamp;
				Bookmarks[index].HasData = true;
				Bookmarks[index].State = BookmarkState.Saved;

			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Save bookmark failed: {ex.Message}");
			}
		}

		public void LoadBookmark(int slotNumber)
		{
			if(slotNumber < 1 || slotNumber > 10) {
				return;
			}

			try {
				int index = slotNumber - 1;
				if(!Bookmarks[index].HasData) {
					return;
				}

				EmuApi.LoadState((uint)(slotNumber - 1));

				Bookmarks[index].State = BookmarkState.Loaded;

				TASEditor.PlaybackControl.UpdateFrameCount();
				TASEditor.FrameList.SelectFrame(TASEditor.PlaybackControl.CurrentFrame);

			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Load bookmark failed: {ex.Message}");
			}
		}
	}

	public class BookmarkViewModel : ReactiveObject
	{
		public int SlotNumber { get; set; }
		public string SlotDisplay => $"Slot {SlotNumber}";

		private int _frameNumber = -1;
		public int FrameNumber
		{
			get => _frameNumber;
			set
			{
				this.RaiseAndSetIfChanged(ref _frameNumber, value);
				this.RaisePropertyChanged(nameof(FrameDisplay));
			}
		}

		[Reactive] public string TimeStamp { get; set; } = "--:--.---";
		[Reactive] public bool HasData { get; set; } = false;
		[Reactive] public BookmarkState State { get; set; } = BookmarkState.None;

		public string FrameDisplay => FrameNumber >= 0 ? FrameNumber.ToString("D6") : "------";

		public BookmarkViewModel(int slotNumber)
		{
			SlotNumber = slotNumber;
		}
	}

	public class BookmarkStateToBrushConverter : IValueConverter
	{
		public static readonly BookmarkStateToBrushConverter Instance = new();

		private static readonly SolidColorBrush BlackBrush = new(Colors.Black);
		private static readonly SolidColorBrush DarkRedBrush = new(Color.FromRgb(0xF0, 0x00, 0x00));
		private static readonly SolidColorBrush DarkGreenBrush = new(Color.FromRgb(0x00, 0xE0, 0x00));

		public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
		{
			if(value is BookmarkState state) {
				return state switch {
					BookmarkState.Saved => DarkRedBrush,
					BookmarkState.Loaded => DarkGreenBrush,
					_ => BlackBrush
				};
			}
			return BlackBrush;
		}

		public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
		{
			throw new NotImplementedException();
		}
	}

	public class BookmarkStateToFontWeightConverter : IValueConverter
	{
		public static readonly BookmarkStateToFontWeightConverter Instance = new();

		public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
		{
			if(value is BookmarkState state) {
				return state == BookmarkState.Saved ? FontWeight.Bold : FontWeight.Normal;
			}
			return FontWeight.Normal;
		}

		public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
		{
			throw new NotImplementedException();
		}
	}
}
