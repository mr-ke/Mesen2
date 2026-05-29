using Avalonia.Collections;
using Mesen.Config;
using Mesen.Debugger.Utilities;
using Mesen.Interop;
using Mesen.Utilities;
using Mesen.ViewModels;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.ComponentModel;

namespace Mesen.Debugger.ViewModels
{
	public class PlaybackControlViewModel : DisposableViewModel
	{
		[Reactive] public bool IsPaused { get; private set; } = false;
		[Reactive] public int CurrentFrame { get; private set; } = 0;
		[Reactive] public int TotalFrames { get; private set; } = 100;

		public CpuType CpuType { get; }
		public TASEditorWindowViewModel TASEditor { get; }

		[Obsolete("For designer only")]
		public PlaybackControlViewModel() : this(CpuType.Snes, new()) { }

		public PlaybackControlViewModel(CpuType cpuType, TASEditorWindowViewModel tasEditor)
		{
			CpuType = cpuType;
			TASEditor = tasEditor;
		}

		public void PreviousMarker()
		{
		}

		public void RewindFrame()
		{
			if(DebugApi.GetDebuggerFeatures(CpuType).StepBack) {
				DebugSharedActions.Step(CpuType, StepType.StepBack, 2);
				UpdateFrameCount();
				TASEditor.FrameList.SelectFrame(CurrentFrame);
			}
		}

		public void TogglePause()
		{
			IsPaused = !IsPaused;
			if(IsPaused) {
				EmuApi.Pause();
				UpdateFrameCount();
				TASEditor.FrameList.SelectFrame(CurrentFrame);
			} else {
				EmuApi.Resume();
			}
		}

		public void AdvanceFrame()
		{
			DebugSharedActions.Step(CpuType, StepType.PpuFrame, 1);
			UpdateFrameCount();
			TASEditor.FrameList.SelectFrame(CurrentFrame);
		}

		public void NextMarker()
		{
		}

		public void UpdateFrameCount()
		{
			TimingInfo timing = EmuApi.GetTimingInfo(CpuType);
			CurrentFrame = (int)timing.FrameCount;
		}
	}
}
