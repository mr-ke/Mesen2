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
	public class TASInputViewModel : DisposableViewModel
	{
		[Reactive] public string InputDisplay { get; private set; } = "No input recorded";

		public CpuType CpuType { get; }
		public TASEditorWindowViewModel TASEditor { get; }

		[Obsolete("For designer only")]
		public TASInputViewModel() : this(CpuType.Snes, new()) { }

		public TASInputViewModel(CpuType cpuType, TASEditorWindowViewModel tasEditor)
		{
			CpuType = cpuType;
			TASEditor = tasEditor;
		}

		public void UpdateInputDisplay()
		{
			InputDisplay = "Frame input display area";
		}
	}
}
