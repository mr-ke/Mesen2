using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using System;
using Mesen.Debugger.ViewModels;
using Mesen.Interop;
using Avalonia.Interactivity;
using System.ComponentModel;
using Avalonia.Threading;
using Mesen.Config;
using System.Runtime.InteropServices;
using Mesen.Debugger.Utilities;
using Mesen.Utilities;
using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.VisualTree;
using Avalonia.Input;
using Mesen.Debugger.Views;
using System.Linq;
using System.IO;

namespace Mesen.Debugger.Windows
{
	public class TASEditorWindow : MesenWindow
	{
		private TASEditorWindowViewModel _model;

		[Obsolete("For designer only")]
		public TASEditorWindow() : this(null) { }

		public TASEditorWindow(CpuType? cpuType)
		{
			InitializeComponent();
#if DEBUG
			this.AttachDevTools();
#endif

			_model = new TASEditorWindowViewModel(cpuType);
			DataContext = _model;

			if(Design.IsDesignMode) {
				return;
			}
			
			_model.Config.LoadWindowSettings(this);
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		protected override void OnOpened(EventArgs e)
		{
			base.OnOpened(e);

			Dispatcher.UIThread.Post(() => {
				_model.FrameList.UpdateFrameList();
			});
		}

		protected override void OnClosing(WindowClosingEventArgs e)
		{
			base.OnClosing(e);

			if(Design.IsDesignMode) {
				return;
			}

			_model.SaveConfig();
			_model.Config.SaveWindowSettings(this);
		}
	}
}
