using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Selection;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using Avalonia.VisualTree;
using DataBoxControl;
using Mesen.Debugger.ViewModels;

namespace Mesen.Debugger.Views
{
	public partial class TASFrameListView : UserControl
	{
		private DataBox? _dataBox;

		public TASFrameListView()
		{
			InitializeComponent();
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

		protected override void OnLoaded(Avalonia.Interactivity.RoutedEventArgs e)
		{
			base.OnLoaded(e);
			_dataBox = this.FindControl<DataBox>("DataBox");
		}

		protected override void OnKeyDown(KeyEventArgs e)
		{
			base.OnKeyDown(e);
		}

		private void OnCellDoubleTapped(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
		{
			if(DataContext is TASFrameListViewModel model && model.HasImportedData) {
				if(sender is Border border && border.DataContext is TASFrameViewModel frame) {
					var parent = border.GetVisualParent();
					while(parent != null) {
						if(parent is DataBoxCell cell) {
							string columnName = cell.Column?.ColumnName ?? "";
							if(columnName != "Frame" && frame.ToggleButton(columnName)) {
								e.Handled = true;
								return;
							}
							break;
						}
						parent = parent.GetVisualParent() as Visual;
					}
				}
			}
		}
	}
}
