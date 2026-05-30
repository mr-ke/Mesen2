using Dock.Avalonia.Controls;
using Dock.Model.Core;
using Mesen.Debugger.ViewModels;
using Mesen.Debugger.ViewModels.DebuggerDock;
using System;
using System.Collections.Generic;
using System.Linq;
using Dock.Model.Controls;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using Dock.Model.Mvvm.Core;

namespace Mesen.Debugger
{
	public class TASEditorDockFactory : Factory
	{
		public ToolContainerViewModel<TASFrameListViewModel> LabelListTool { get; private set; }
		public ToolContainerViewModel<TASInputViewModel> InputDisplayTool { get; private set; }
		public ToolContainerViewModel<PlaybackControlViewModel> PlaybackControlTool { get; private set; }
		public ToolContainerViewModel<BookmarksViewModel> BookmarksTool { get; private set; }

		private DockEntryDefinition? _savedRootDef;

		public TASEditorDockFactory(DockEntryDefinition? savedRootDef)
		{
			LabelListTool = new("Frame List");
			LabelListTool.CanClose = false;
			
			InputDisplayTool = new("Input Display");
			InputDisplayTool.CanClose = false;

			PlaybackControlTool = new("Playback Controls");
			PlaybackControlTool.CanClose = false;

			BookmarksTool = new("Bookmarks");
			BookmarksTool.CanClose = false;

			_savedRootDef = savedRootDef;
		}

		public override IRootDock CreateLayout()
		{
			if(_savedRootDef != null) {
				try {
					if(FromDockDefinition(_savedRootDef) is IRootDock savedRootLayout) {
						return savedRootLayout;
					}
				} catch {
				}
			}

			return GetDefaultLayout();
		}

		public IRootDock GetDefaultLayout()
		{
			var mainLayout = new ProportionalDock {
				Orientation = Orientation.Horizontal,
				VisibleDockables = CreateList<IDockable>(
					new ToolDock {
						Proportion = 0.80,
						VisibleDockables = CreateList<IDockable>(LabelListTool)
					},
					new MesenProportionalDockSplitter(),
					new ProportionalDock {
						Proportion = 0.20,
						Orientation = Orientation.Vertical,
						VisibleDockables = CreateList<IDockable>(
							new ToolDock {
								Proportion = 0.45,
								VisibleDockables = CreateList<IDockable>(PlaybackControlTool)
							},
							new MesenProportionalDockSplitter(),
							new ToolDock {
								Proportion = 0.45,
								VisibleDockables = CreateList<IDockable>(BookmarksTool)
							},
							new MesenProportionalDockSplitter(),
							new ToolDock {
								Proportion = 0.10,
								VisibleDockables = CreateList<IDockable>(InputDisplayTool)
							}
						)
					}
				)
			};

			var root = CreateRootDock();
			root.ActiveDockable = mainLayout;
			root.DefaultDockable = mainLayout;
			root.VisibleDockables = CreateList<IDockable>(mainLayout);
			return root;
		}

		public override IProportionalDockSplitter CreateProportionalDockSplitter()
		{
			return new MesenProportionalDockSplitter();
		}

		public override void InitLayout(IDockable layout)
		{
			this.ContextLocator = new Dictionary<string, Func<object?>> {
			};

			this.HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
				[nameof(IDockWindow)] = () => new HostWindow()
			};

			this.DockableLocator = new Dictionary<string, Func<IDockable?>> { };

			base.InitLayout(layout);
		}

		public DockEntryDefinition ToDockDefinition(IDockable dockable)
		{
			DockEntryDefinition entry = new();
			if(dockable is MesenProportionalDockSplitter) {
				entry.Type = DockEntryType.Splitter;
			} else if(dockable is IDock dock) {
				if(dock is IRootDock) {
					entry.Type = DockEntryType.Root;
				} else if(dock is IProportionalDock propDock) {
					entry.Type = DockEntryType.ProportionalDock;
					entry.Orientation = propDock.Orientation;
				} else {
					entry.Type = DockEntryType.ToolDock;
				}
				entry.Name = dock.Title;
				entry.Proportion = double.IsNaN(dock.Proportion) ? 0 : dock.Proportion;
				entry.Children = new();
				if(dock.VisibleDockables != null) {
					if(dock is IProportionalDock propDock && dock.VisibleDockables.Count == 1) {
						DockEntryDefinition innerEntry = ToDockDefinition(dock.VisibleDockables[0]);
						innerEntry.Proportion = entry.Proportion;
						return innerEntry;
					}

					if(dock.ActiveDockable != null) {
						int index = dock.VisibleDockables.IndexOf(dock.ActiveDockable);
						if(index >= 0) {
							entry.SelectedIndex = index;
						}
					}
					foreach(IDockable child in dock.VisibleDockables) {
						entry.Children.Add(ToDockDefinition(child));
					}
				}
			} else if(dockable is ITool tool) {
				entry.Type = DockEntryType.Tool;
				entry.Name = tool.Title;
				entry.ToolTypeName = tool.GetType().GetGenericArguments()[0].Name;
			}

			return entry;
		}

		public IDockable? FromDockDefinition(DockEntryDefinition def)
		{
			IDockable? dockable = null;
			switch(def.Type) {
				case DockEntryType.Splitter: return CreateProportionalDockSplitter();
				case DockEntryType.Root: dockable = CreateRootDock(); break;
				case DockEntryType.ProportionalDock:
					dockable = new ProportionalDock() { Orientation = def.Orientation };
					break;
				case DockEntryType.ToolDock: dockable = new ToolDock(); break;
				case DockEntryType.Tool:
					return GetTool(def.ToolTypeName);
			}

			if(dockable is IDock dock) {
				dock.Title = def.Name;
				dock.Proportion = def.Proportion;
				if(def.Children != null) {
					dock.VisibleDockables = CreateList<IDockable>();
					foreach(DockEntryDefinition child in def.Children) {
						IDockable? childDockable = FromDockDefinition(child);
						if(childDockable != null) {
							dock.VisibleDockables.Add(childDockable);
						}
					}
					if(def.SelectedIndex >= 0 && def.SelectedIndex < dock.VisibleDockables.Count) {
						dock.ActiveDockable = dock.VisibleDockables[def.SelectedIndex];
					}
				}
			}

			return dockable;
		}

		private ITool? GetTool(string? toolTypeName)
		{
			return toolTypeName switch {
				nameof(TASFrameListViewModel) => LabelListTool,
				nameof(TASInputViewModel) => InputDisplayTool,
				nameof(PlaybackControlViewModel) => PlaybackControlTool,
				nameof(BookmarksViewModel) => BookmarksTool,
				_ => null
			};
		}
	}
}
