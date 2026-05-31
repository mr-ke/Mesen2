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
		[Reactive] public BookmarksViewModel Bookmarks { get; private set; }

		[Reactive] public TASEditorDockFactory DockFactory { get; private set; }
		[Reactive] public IRootDock DockLayout { get; private set; }

		public Window? Window { get; set; }

		[Reactive] public List<ContextMenuAction> ToolbarItems { get; private set; } = new();
		
		[Reactive] public List<ContextMenuAction> FileMenuItems { get; private set; } = new();
		[Reactive] public List<ContextMenuAction> EditMenuItems { get; private set; } = new();
		[Reactive] public List<ContextMenuAction> ViewMenuItems { get; private set; } = new();
		[Reactive] public List<ContextMenuAction> HelpMenuItems { get; private set; } = new();

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
			Bookmarks = AddDisposable(new BookmarksViewModel(CpuType, this));

			DockFactory = new TASEditorDockFactory(Config.TASSavedDockLayout);

			DockFactory.LabelListTool.Model = FrameList;
			DockFactory.InputDisplayTool.Model = InputDisplay;
			DockFactory.PlaybackControlTool.Model = PlaybackControl;
			DockFactory.BookmarksTool.Model = Bookmarks;

			DockLayout = DockFactory.CreateLayout();
			DockFactory.InitLayout(DockLayout);

			InitMenus();
		}

		public void CheckAndLoadLastMMO()
		{
			if(Design.IsDesignMode) {
				return;
			}

			var recentMMOs = ConfigManager.Config.Debug.Debugger.RecentMMOs;
			if(recentMMOs != null && recentMMOs.Count > 0) {
				string lastMMO = recentMMOs[0];
				if(!string.IsNullOrEmpty(lastMMO) && File.Exists(lastMMO)) {
					ShowLastMMODialog(lastMMO);
				}
			}
		}

		private async void ShowLastMMODialog(string lastMMO)
		{
			var result = await CountdownMessageBox.Show(
				Window,
				$"Would you like to open the last MMO file?\n\n{Path.GetFileName(lastMMO)}",
				"Open Last MMO",
				5
			);
			
			if(result) {
				FrameList.ImportMMO(lastMMO);
			}
		}

		private void InitMenus()
		{
			FileMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.Custom,
					CustomText = "New",
					OnClick = () => NewProject()
				},
				new ContextMenuAction() {
					ActionType = ActionType.Open,
					OnClick = () => ImportMMO()
				},
				new ContextMenuAction() {
					ActionType = ActionType.SaveAs,
					OnClick = () => ExportMMO(),
					IsEnabled = () => FrameList.HasImportedData
				},
				new ContextMenuAction() {
					ActionType = ActionType.RecentFiles,
					SubActions = GetRecentFilesMenu()
				},
				new ContextMenuAction() {
					ActionType = ActionType.Export,
					CustomText = "Export to BK2",
					OnClick = () => { },
					IsEnabled = () => false
				},
				new ContextMenuAction() {
					ActionType = ActionType.Exit,
					OnClick = () => Window?.Close()
				},
			};

			EditMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.Undo,
					OnClick = () => { },
					IsEnabled = () => false
				},
				new ContextMenuAction() {
					ActionType = ActionType.Redo,
					OnClick = () => { },
					IsEnabled = () => false
				},
				new ContextMenuAction() {
					ActionType = ActionType.GoTo,
					CustomText = "Go to Frame",
					OnClick = () => GoToFrame(),
					IsEnabled = () => FrameList.HasImportedData
				},
			};

			ViewMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.ResetLayout,
					OnClick = () => ResetLayout()
				},
			};

			HelpMenuItems = new List<ContextMenuAction> {
				new ContextMenuAction() {
					ActionType = ActionType.About,
					OnClick = () => ShowAbout()
				},
			};
		}

		private List<object> GetRecentFilesMenu()
		{
			List<object> recentFiles = new List<object>();
			var recentMMOs = ConfigManager.Config.Debug.Debugger.RecentMMOs;
			
			if(recentMMOs.Count == 0) {
				recentFiles.Add(new ContextMenuAction() {
					ActionType = ActionType.Custom,
					CustomText = "(No recent files)",
					IsEnabled = () => false
				});
			} else {
				foreach(string file in recentMMOs.Take(10)) {
					string filePath = file;
					recentFiles.Add(new ContextMenuAction() {
						ActionType = ActionType.Custom,
						CustomText = Path.GetFileName(filePath),
						OnClick = () => FrameList.ImportMMO(filePath)
					});
				}
			}
			
			return recentFiles;
		}

		private void NewProject()
		{
			FrameList.CreateNewProject(1000);
		}

		private async void GoToFrame()
		{
			if(!FrameList.HasImportedData) {
				return;
			}

			string? input = await InputDialog.Show(Window, "Go to Frame", "Enter frame number:", "0");
			if(int.TryParse(input, out int frameNumber) && frameNumber >= 0) {
				FrameList.SelectFrame(frameNumber);
			}
		}

		private async void ShowAbout()
		{
			string aboutText = @"TAS Editor Help

TAS Editor allows you to create and edit Tool-Assisted Speedruns (TAS).

Basic Usage:
1. File -> New: Create a new empty project with 1000 frames
2. File -> Open: Import an existing MMO file
3. File -> Save As: Export your project to MMO format

Playback Controls:
- Play/Pause: Start or pause movie playback
- Stop: Stop playback and return to start
- Step Forward/Backward: Advance or rewind one frame

Bookmarks:
- Right-click a slot to save the current state
- Double-click a slot to load the saved state
- Frame number and timestamp are displayed for each bookmark

Frame List:
- Click a frame to select it
- Double-click to play from that frame
- Use bookmarks to quickly navigate to important frames

Tips:
- Use bookmarks to save key moments in your TAS
- The frame counter shows your current position
- Export to BK2 format for compatibility with other tools";

			await MessageBox.Show(Window, aboutText, "About TAS Editor", MessageBoxButtons.OK, MessageBoxIcon.Info);
		}

		private async void ImportMMO()
		{
			string? file = await FileDialogHelper.OpenFile(null, Window, FileDialogHelper.MesenMovieExt);
			if(!string.IsNullOrEmpty(file)) {
				FrameList.ImportMMO(file);
				AddToRecentFiles(file);
			}
		}

		private void AddToRecentFiles(string filePath)
		{
			var recentFiles = ConfigManager.Config.Debug.Debugger.RecentMMOs;
			recentFiles.RemoveAll(f => f == filePath);
			recentFiles.Insert(0, filePath);
			if(recentFiles.Count > 10) {
				recentFiles.RemoveRange(10, recentFiles.Count - 10);
			}
			ConfigManager.Config.Save();
		}

		private async void ExportMMO()
		{
			string? file = await FileDialogHelper.SaveFile(null, null, Window, FileDialogHelper.MesenMovieExt);
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
				Config.TASSavedDockLayout = DockFactory.ToDockDefinition(DockLayout);
			}
			ConfigManager.Config.Save();
		}
	}
}
