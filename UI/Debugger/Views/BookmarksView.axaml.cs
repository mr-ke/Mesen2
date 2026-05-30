using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using Avalonia.VisualTree;
using DataBoxControl;
using Mesen.Debugger.ViewModels;

namespace Mesen.Debugger.Views
{
	public partial class BookmarksView : UserControl
	{
		public BookmarksView()
		{
			InitializeComponent();
			AddHandler(DataBoxRow.DoubleTappedEvent, OnRowDoubleTapped, Avalonia.Interactivity.RoutingStrategies.Bubble);
			AddHandler(PointerPressedEvent, OnPointerPressed, Avalonia.Interactivity.RoutingStrategies.Tunnel);
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
		{
			if(e.GetCurrentPoint(this).Properties.IsRightButtonPressed) {
				if(DataContext is BookmarksViewModel model) {
					var visual = e.Source as Visual;
					while(visual != null) {
						if(visual is DataBoxRow row && row.DataContext is BookmarkViewModel bookmark) {
							model.SaveBookmark(bookmark.SlotNumber);
							e.Handled = true;
							return;
						}
						visual = visual.GetVisualParent() as Visual;
					}
				}
			}
		}

		private void OnRowDoubleTapped(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
		{
			if(DataContext is BookmarksViewModel model) {
				if(model.Bookmarks.Count > 0) {
					var dataBox = this.FindControl<DataBox>("BookmarksDataBox");
					if(dataBox != null && dataBox.Selection != null && dataBox.Selection.SelectedIndexes.Count > 0) {
						int selectedIndex = dataBox.Selection.SelectedIndexes[0];
						if(selectedIndex >= 0 && selectedIndex < model.Bookmarks.Count) {
							model.LoadBookmark(model.Bookmarks[selectedIndex].SlotNumber);
						}
					}
				}
			}
		}
	}
}
