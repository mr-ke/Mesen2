using Avalonia;
using Avalonia.Collections;
using Avalonia.Controls;
using Avalonia.Controls.Selection;
using Avalonia.Media;
using Avalonia.Styling;
using DataBoxControl;
using Mesen.Config;
using Mesen.Debugger.Labels;
using Mesen.Debugger.Utilities;
using Mesen.Debugger.Windows;
using Mesen.Interop;
using Mesen.Utilities;
using Mesen.ViewModels;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Linq;

namespace Mesen.Debugger.ViewModels
{
	public enum RowState
	{
		Empty,
		Loaded
	}

	public enum CellState
	{
		Empty,
		Loaded,
		Modified
	}

	public class CellStateToBrushConverter : Avalonia.Data.Converters.IValueConverter
	{
		public static readonly CellStateToBrushConverter Instance = new();
		
		private static readonly SolidColorBrush LightWhiteBrush = new(Colors.White);
		private static readonly SolidColorBrush LightGreenBrush = new(Color.FromRgb(0x90, 0xEE, 0x90));
		private static readonly SolidColorBrush LightPinkBrush = new(Color.FromRgb(0xFF, 0xB6, 0xC1));
		
		private static readonly SolidColorBrush DarkWhiteBrush = new(Color.FromRgb(0x40, 0x40, 0x40));
		private static readonly SolidColorBrush DarkGreenBrush = new(Color.FromRgb(0x2E, 0x7D, 0x32));
		private static readonly SolidColorBrush DarkPinkBrush = new(Color.FromRgb(0x8B, 0x3A, 0x3A));

		public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
		{
			bool isDarkTheme = Application.Current?.ActualThemeVariant == ThemeVariant.Dark;
			
			if(value is CellState state) {
				return state switch {
					CellState.Loaded => isDarkTheme ? DarkGreenBrush : LightGreenBrush,
					CellState.Modified => isDarkTheme ? DarkPinkBrush : LightPinkBrush,
					_ => isDarkTheme ? DarkWhiteBrush : LightWhiteBrush
				};
			}
			return isDarkTheme ? DarkWhiteBrush : LightWhiteBrush;
		}

		public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
		{
			throw new NotImplementedException();
		}
	}
	public class TASFrameListViewModel : DisposableViewModel
	{
		[Reactive] public MesenList<TASFrameViewModel> Frames { get; private set; } = new();
		[Reactive] public SelectionModel<TASFrameViewModel?> Selection { get; set; } = new() { SingleSelect = false };
		[Reactive] public SortState SortState { get; set; } = new();
		public List<int> ColumnWidths { get; } = new List<int> { 60, 30, 30, 30, 30, 30, 30, 30, 30 };

		public CpuType CpuType { get; }
		public TASEditorWindowViewModel TASEditor { get; }
		public string? ImportedMMOPath { get; private set; }
		public bool HasImportedData { get; private set; } = false;

		[Obsolete("For designer only")]
		public TASFrameListViewModel() : this(CpuType.Snes, new()) { }

		public TASFrameListViewModel(CpuType cpuType, TASEditorWindowViewModel tasEditor)
		{
			CpuType = cpuType;
			TASEditor = tasEditor;

			SortState.SetColumnSort("Frame", ListSortDirection.Ascending, true);
		}

		public void Sort(object? param)
		{
			UpdateFrameList();
		}

		private Dictionary<string, Func<TASFrameViewModel, TASFrameViewModel, int>> _comparers = new() {
			{ "Frame", (a, b) => a.FrameNumber.CompareTo(b.FrameNumber) },
			{ "A", (a, b) => a.ButtonA.CompareTo(b.ButtonA) },
			{ "B", (a, b) => a.ButtonB.CompareTo(b.ButtonB) },
			{ "S", (a, b) => a.ButtonS.CompareTo(b.ButtonS) },
			{ "T", (a, b) => a.ButtonT.CompareTo(b.ButtonT) },
			{ "U", (a, b) => a.ButtonU.CompareTo(b.ButtonU) },
			{ "D", (a, b) => a.ButtonD.CompareTo(b.ButtonD) },
			{ "L", (a, b) => a.ButtonL.CompareTo(b.ButtonL) },
			{ "R", (a, b) => a.ButtonR.CompareTo(b.ButtonR) },
		};

		public void UpdateFrameList()
		{
			int currentFrame = TASEditor.PlaybackControl.CurrentFrame;
			int maxFrame = Math.Max(currentFrame + 100, 1000);
			
			List<int> selectedIndexes = Selection.SelectedIndexes.ToList();

			List<TASFrameViewModel> frames = new List<TASFrameViewModel>();
			for(int i = 0; i < maxFrame; i++) {
				frames.Add(new TASFrameViewModel(i));
			}

			SortHelper.SortList(frames, SortState.SortOrder, _comparers, "Frame");

			Frames.Replace(frames);

			Selection.SelectIndexes(selectedIndexes, Frames.Count);
		}

		public void SelectFrame(int frameNumber)
		{
			if(!HasImportedData && frameNumber >= Frames.Count) {
				UpdateFrameList();
			}
			
			if(frameNumber >= 0 && frameNumber < Frames.Count) {
				Selection.Clear();
				Selection.Select(frameNumber);
			} else if(Frames.Count > 0) {
				Selection.Clear();
				Selection.Select(Frames.Count - 1);
			}
		}

		public void RefreshFrameList()
		{
			foreach(TASFrameViewModel frame in Frames) {
				frame.Refresh();
			}
		}

		public void InitContextMenu(Control parent)
		{
			AddDisposables(DebugShortcutManager.CreateContextMenu(parent, new object[] {
				new ContextMenuAction() {
					ActionType = ActionType.Add,
					Shortcut = () => ConfigManager.Config.Debug.Shortcuts.Get(DebuggerShortcut.LabelList_Add),
					OnClick = () => { }
				},

				new ContextMenuAction() {
					ActionType = ActionType.Edit,
					Shortcut = () => ConfigManager.Config.Debug.Shortcuts.Get(DebuggerShortcut.LabelList_Edit),
					IsEnabled = () => Selection.SelectedItems.Count == 1,
					OnClick = () => { }
				},

				new ContextMenuAction() {
					ActionType = ActionType.Delete,
					Shortcut = () => ConfigManager.Config.Debug.Shortcuts.Get(DebuggerShortcut.LabelList_Delete),
					IsEnabled = () => Selection.SelectedItems.Count > 0,
					OnClick = () => { }
				},
			}));
		}

		public void ImportMMO(string mmoFilePath)
		{
			try {
				List<TASInputFrame> inputFrames = MMOFileHandler.ImportMMO(mmoFilePath);
				
				List<TASFrameViewModel> frames = new List<TASFrameViewModel>();
				for(int i = 0; i < inputFrames.Count; i++) {
					TASFrameViewModel frame = new TASFrameViewModel(i);
					TASInputFrame input = inputFrames[i];
					frame.SetOriginalState(
						input.ButtonA, input.ButtonB, input.Select, input.Start,
						input.Up, input.Down, input.Left, input.Right
					);
					frames.Add(frame);
				}

				Frames.Replace(frames);
				ImportedMMOPath = mmoFilePath;
				HasImportedData = true;
			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Import MMO failed: {ex.Message}");
			}
		}

		public void CreateNewProject(int frameCount)
		{
			try {
				List<TASFrameViewModel> frames = new List<TASFrameViewModel>();
				for(int i = 0; i < frameCount; i++) {
					frames.Add(new TASFrameViewModel(i));
				}

				Frames.Replace(frames);
				ImportedMMOPath = null;
				HasImportedData = true;
			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Create new project failed: {ex.Message}");
			}
		}

		public void ExportMMO(string mmoFilePath)
		{
			try {
				List<TASInputFrame> inputFrames = new List<TASInputFrame>();
				foreach(TASFrameViewModel frame in Frames) {
					TASInputFrame input = new TASInputFrame {
						ButtonA = frame.ButtonA,
						ButtonB = frame.ButtonB,
						Select = frame.ButtonS,
						Start = frame.ButtonT,
						Up = frame.ButtonU,
						Down = frame.ButtonD,
						Left = frame.ButtonL,
						Right = frame.ButtonR
					};
					inputFrames.Add(input);
				}

				MMOFileHandler.ExportMMO(mmoFilePath, inputFrames, ImportedMMOPath);
			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Export MMO failed: {ex.Message}");
			}
		}
	}

	public class TASFrameViewModel : INotifyPropertyChanged
	{
		public int FrameNumber { get; set; }
		public string FrameDisplay => FrameNumber.ToString("D6");
		
		private string _buttonA = ".";
		private string _buttonB = ".";
		private string _buttonS = ".";
		private string _buttonT = ".";
		private string _buttonU = ".";
		private string _buttonD = ".";
		private string _buttonL = ".";
		private string _buttonR = ".";

		private string _originalA = ".";
		private string _originalB = ".";
		private string _originalS = ".";
		private string _originalT = ".";
		private string _originalU = ".";
		private string _originalD = ".";
		private string _originalL = ".";
		private string _originalR = ".";

		private RowState _rowState = RowState.Empty;

		public RowState RowState
		{
			get => _rowState;
			private set
			{
				_rowState = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(RowState)));
				RefreshCellStates();
			}
		}

		public string ButtonA 
		{ 
			get => _buttonA;
			private set 
			{
				_buttonA = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonA)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateA)));
			}
		}
		public string ButtonB 
		{ 
			get => _buttonB;
			private set 
			{
				_buttonB = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonB)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateB)));
			}
		}
		public string ButtonS 
		{ 
			get => _buttonS;
			private set 
			{
				_buttonS = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonS)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateS)));
			}
		}
		public string ButtonT 
		{ 
			get => _buttonT;
			private set 
			{
				_buttonT = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonT)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateT)));
			}
		}
		public string ButtonU 
		{ 
			get => _buttonU;
			private set 
			{
				_buttonU = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonU)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateU)));
			}
		}
		public string ButtonD 
		{ 
			get => _buttonD;
			private set 
			{
				_buttonD = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonD)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateD)));
			}
		}
		public string ButtonL 
		{ 
			get => _buttonL;
			private set 
			{
				_buttonL = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonL)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateL)));
			}
		}
		public string ButtonR 
		{ 
			get => _buttonR;
			private set 
			{
				_buttonR = value;
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonR)));
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateR)));
			}
		}

		public CellState StateA => GetCellState(_buttonA, _originalA);
		public CellState StateB => GetCellState(_buttonB, _originalB);
		public CellState StateS => GetCellState(_buttonS, _originalS);
		public CellState StateT => GetCellState(_buttonT, _originalT);
		public CellState StateU => GetCellState(_buttonU, _originalU);
		public CellState StateD => GetCellState(_buttonD, _originalD);
		public CellState StateL => GetCellState(_buttonL, _originalL);
		public CellState StateR => GetCellState(_buttonR, _originalR);

		private CellState GetCellState(string current, string original)
		{
			if(current != original) {
				return CellState.Modified;
			}
			return _rowState == RowState.Loaded ? CellState.Loaded : CellState.Empty;
		}

		private void RefreshCellStates()
		{
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateA)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateB)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateS)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateT)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateU)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateD)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateL)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StateR)));
		}

		public object RowBrush => AvaloniaProperty.UnsetValue;
		public FontStyle RowStyle => FontStyle.Normal;

		public event PropertyChangedEventHandler? PropertyChanged;

		public void Refresh()
		{
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonA)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonB)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonS)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonT)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonU)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonD)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonL)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ButtonR)));
		}

		public void SetOriginalState(string buttonA, string buttonB, string buttonS, string buttonT, 
			string buttonU, string buttonD, string buttonL, string buttonR)
		{
			_originalA = buttonA;
			_originalB = buttonB;
			_originalS = buttonS;
			_originalT = buttonT;
			_originalU = buttonU;
			_originalD = buttonD;
			_originalL = buttonL;
			_originalR = buttonR;
			
			_buttonA = buttonA;
			_buttonB = buttonB;
			_buttonS = buttonS;
			_buttonT = buttonT;
			_buttonU = buttonU;
			_buttonD = buttonD;
			_buttonL = buttonL;
			_buttonR = buttonR;
			
			RowState = RowState.Loaded;
		}

		public void SetButtonState(string buttonA, string buttonB, string buttonS, string buttonT, 
			string buttonU, string buttonD, string buttonL, string buttonR)
		{
			ButtonA = buttonA;
			ButtonB = buttonB;
			ButtonS = buttonS;
			ButtonT = buttonT;
			ButtonU = buttonU;
			ButtonD = buttonD;
			ButtonL = buttonL;
			ButtonR = buttonR;
		}

		public bool ToggleButton(string columnName)
		{
			switch(columnName) {
				case "A": ButtonA = ToggleValue(ButtonA, "A"); return true;
				case "B": ButtonB = ToggleValue(ButtonB, "B"); return true;
				case "S": ButtonS = ToggleValue(ButtonS, "S"); return true;
				case "T": ButtonT = ToggleValue(ButtonT, "T"); return true;
				case "U": ButtonU = ToggleValue(ButtonU, "U"); return true;
				case "D": ButtonD = ToggleValue(ButtonD, "D"); return true;
				case "L": ButtonL = ToggleValue(ButtonL, "L"); return true;
				case "R": ButtonR = ToggleValue(ButtonR, "R"); return true;
			}
			return false;
		}

		private string ToggleValue(string currentValue, string buttonName)
		{
			return currentValue == "." ? buttonName : ".";
		}

		public TASFrameViewModel(int frameNumber)
		{
			FrameNumber = frameNumber;
		}
	}
}
