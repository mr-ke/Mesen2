using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm.Controls;
using Mesen.Config;
using Mesen.Debugger.Disassembly;
using Mesen.Debugger.Integration;
using Mesen.Debugger.Labels;
using Mesen.Debugger.StatusViews;
using Mesen.Debugger.Utilities;
using Mesen.Debugger.ViewModels.DebuggerDock;
using Mesen.Debugger.Windows;
using Mesen.Interop;
using Mesen.Localization;
using Mesen.Utilities;
using Mesen.ViewModels;
using Mesen.Windows;
using ReactiveUI;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Reactive;
using System.Threading.Tasks;

namespace Mesen.Debugger.ViewModels
{
	public class TASEditorWindowViewModel : DisposableViewModel
	{
		[Reactive] public string Title { get; private set; } = "TAS Editor";
		[Reactive] public WindowIcon? Icon { get; private set; } = null;

		[Reactive] public DebuggerConfig Config { get; private set; }

		[Reactive] public DebuggerOptionsViewModel Options { get; private set; }

		[Reactive] public TASFrameListViewModel FrameList { get; private set; }
		[Reactive] public TASInputViewModel InputDisplay { get; private set; }
		[Reactive] public PlaybackControlViewModel PlaybackControl { get; private set; }

		[Reactive] public TASEditorDockFactory DockFactory { get; private set; }
		[Reactive] public IRootDock DockLayout { get; private set; }

		public Window? Window { get; set; }

		[Reactive] public List<ContextMenuAction> ToolbarItems { get; private set; } = new();
		
		[Reactive] public List<ContextMenuAction> FileMenuItems { get; private set; } = new();
		[Reactive] public List<ContextMenuAction> EditMenuItems { get; private set; } = new();
		[Reactive] public List<ContextMenuAction> ViewMenuItems { get; private set; } = new();

		public CpuType CpuType { get; private set; }

		[Obsolete("For designer only")]
		public TASEditorWindowViewModel() : this(null) { }

		public TASEditorWindowViewModel(CpuType? cpuType)
		{
			ConsoleType consoleType;
			if(Design.IsDesignMode) {
				CpuType = CpuType.Snes;
				consoleType = ConsoleType.Snes;
			} else {
				RomInfo romInfo = EmuApi.GetRomInfo();
				consoleType = romInfo.ConsoleType;
				if(cpuType != null) {
					CpuType = cpuType.Value;
				} else {
					CpuType = romInfo.ConsoleType.GetMainCpuType();
				}
				Icon = new WindowIcon(ImageUtilities.BitmapFromAsset("Assets/Movie.png"));
			}

			Config = ConfigManager.Config.Debug.Debugger;

			Options = new DebuggerOptionsViewModel(Config, CpuType);
			FrameList = AddDisposable(new TASFrameListViewModel(CpuType, this));
			InputDisplay = AddDisposable(new TASInputViewModel(CpuType, this));
			PlaybackControl = AddDisposable(new PlaybackControlViewModel(CpuType, this));

			DockFactory = new TASEditorDockFactory(Config.SavedDockLayout);

			DockFactory.LabelListTool.Model = FrameList;
			DockFactory.InputDisplayTool.Model = InputDisplay;
			DockFactory.PlaybackControlTool.Model = PlaybackControl;

			DockLayout = DockFactory.CreateLayout();
			DockFactory.InitLayout(DockLayout);

			InitMenus();
		}

		private void InitMenus()
		{
			FileMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.Add,
					OnClick = () => { }
				},
				new ContextMenuAction() {
					ActionType = ActionType.Import,
					OnClick = () => ImportMMO()
				},
				new ContextMenuAction() {
					ActionType = ActionType.Export,
					OnClick = () => ExportMMO()
				},
			};

			EditMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.Undo,
					OnClick = () => { }
				},
				new ContextMenuAction() {
					ActionType = ActionType.Copy,
					OnClick = () => { }
				},
			};

			ViewMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.ResetLayout,
					OnClick = () => ResetLayout()
				},
			};
		}

		private async void ImportMMO()
		{
			string? file = await FileDialogHelper.OpenFile(null, Window, "*.mmo", "*.MMO");
			if(!string.IsNullOrEmpty(file)) {
				FrameList.ImportMMO(file);
			}
		}

		private async void ExportMMO()
		{
			string? file = await FileDialogHelper.SaveFile(null, null, Window, "*.mmo", "*.MMO");
			if(!string.IsNullOrEmpty(file)) {
				FrameList.ExportMMO(file);
			}
		}

		public void ResetLayout()
		{
			DockLayout = DockFactory.GetDefaultLayout();
			DockFactory.InitLayout(DockLayout);
		}

		public void SaveConfig()
		{
			if(DockLayout != null) {
				Config.SavedDockLayout = DockFactory.ToDockDefinition(DockLayout);
			}
		}
	}
}
