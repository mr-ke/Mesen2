using Avalonia;
using Avalonia.Collections;
using Avalonia.Controls;
using Avalonia.Controls.Selection;
using Avalonia.Media;
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
using System.Linq;

namespace Mesen.Debugger.ViewModels
{
	public class TASFrameListViewModel : DisposableViewModel
	{
		[Reactive] public MesenList<TASFrameViewModel> Frames { get; private set; } = new();
		[Reactive] public SelectionModel<TASFrameViewModel?> Selection { get; set; } = new() { SingleSelect = false };
		[Reactive] public SortState SortState { get; set; } = new();
		public List<int> ColumnWidths { get; } = new List<int> { 60, 30, 30, 30, 30, 30, 30, 30, 30 };

		public CpuType CpuType { get; }
		public TASEditorWindowViewModel TASEditor { get; }

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
			List<int> selectedIndexes = Selection.SelectedIndexes.ToList();

			List<TASFrameViewModel> frames = new List<TASFrameViewModel>();
			for(int i = 0; i < 100; i++) {
				frames.Add(new TASFrameViewModel(i));
			}

			SortHelper.SortList(frames, SortState.SortOrder, _comparers, "Frame");

			Frames.Replace(frames);

			Selection.SelectIndexes(selectedIndexes, Frames.Count);
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
	}

	public class TASFrameViewModel : INotifyPropertyChanged
	{
		public int FrameNumber { get; set; }
		public string FrameDisplay => FrameNumber.ToString("D6");
		
		public string ButtonA { get; private set; } = ".";
		public string ButtonB { get; private set; } = ".";
		public string ButtonS { get; private set; } = ".";
		public string ButtonT { get; private set; } = ".";
		public string ButtonU { get; private set; } = ".";
		public string ButtonD { get; private set; } = ".";
		public string ButtonL { get; private set; } = ".";
		public string ButtonR { get; private set; } = ".";

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

		public TASFrameViewModel(int frameNumber)
		{
			FrameNumber = frameNumber;
		}
	}
}
