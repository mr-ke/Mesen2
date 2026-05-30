using System;
using Avalonia.Controls;
using Avalonia.Controls.Selection;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using DataBoxControl;
using Mesen.Debugger.ViewModels;

namespace Mesen.Debugger.Views
{
	public partial class TASFrameListView : UserControl
	{
		public TASFrameListView()
		{
			InitializeComponent();
			AddHandler(DataBoxRow.DoubleTappedEvent, OnRowDoubleTapped, Avalonia.Interactivity.RoutingStrategies.Bubble);
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		protected override void OnDataContextChanged(EventArgs e)
		{
			base.OnDataContextChanged(e);
			if(DataContext is TASFrameListViewModel model) {
				model.InitContextMenu(this);
			}
		}

		protected override void OnKeyDown(KeyEventArgs e)
		{
			base.OnKeyDown(e);
		}

		private void OnRowDoubleTapped(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
		{
			if(DataContext is TASFrameListViewModel model && model.HasImportedData) {
				if(model.Selection.SelectedIndexes.Count > 0) {
					int selectedIndex = model.Selection.SelectedIndexes[0];
					if(selectedIndex >= 0 && selectedIndex < model.Frames.Count) {
						model.TASEditor.PlaybackControl.PlayFromFrame(selectedIndex);
					}
				}
			}
		}
	}
}
