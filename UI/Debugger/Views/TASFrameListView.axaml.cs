using System;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using Mesen.Debugger.ViewModels;

namespace Mesen.Debugger.Views
{
	public partial class TASFrameListView : UserControl
	{
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

		protected override void OnKeyDown(KeyEventArgs e)
		{
			base.OnKeyDown(e);
		}
	}
}
