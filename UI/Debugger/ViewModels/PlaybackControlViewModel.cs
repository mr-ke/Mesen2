using Avalonia.Collections;
using Mesen.Config;
using Mesen.Debugger.Utilities;
using Mesen.Debugger.Windows;
using Mesen.Interop;
using Mesen.Utilities;
using Mesen.ViewModels;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;

namespace Mesen.Debugger.ViewModels
{
	public class PlaybackControlViewModel : DisposableViewModel
	{
		[Reactive] public bool IsPaused { get; private set; } = false;
		[Reactive] public int CurrentFrame { get; private set; } = 0;
		[Reactive] public int TotalFrames { get; private set; } = 100;
		[Reactive] public bool IsPlaying { get; private set; } = false;

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

		public void PlayTAS()
		{
			if(!TASEditor.FrameList.HasImportedData) {
				return;
			}

			try {
				if(IsPaused) {
					EmuApi.Resume();
					IsPaused = false;
				}

				string tempFile = Path.Combine(Path.GetTempPath(), "TAS_Temp.mmo");
				TASEditor.FrameList.ExportMMO(tempFile);
				
				RecordApi.MoviePlay(tempFile);
				IsPlaying = true;
			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Play TAS failed: {ex.Message}");
			}
		}

		public void StopTAS()
		{
			if(IsPlaying) {
				RecordApi.MovieStop();
				EmuApi.Pause();
				IsPlaying = false;
				IsPaused = true;
				
				UpdateFrameCount();
				TASEditor.FrameList.SelectFrame(CurrentFrame);
			}
		}

		public void CheckPlaybackState()
		{
			if(IsPlaying && !RecordApi.MoviePlaying()) {
				EmuApi.Pause();
				IsPlaying = false;
				IsPaused = true;
				
				if(TASEditor.FrameList.HasImportedData && TASEditor.FrameList.Frames.Count > 0) {
					CurrentFrame = TASEditor.FrameList.Frames.Count - 1;
				} else {
					UpdateFrameCount();
				}
				
				TASEditor.FrameList.SelectFrame(CurrentFrame);
			}

			if(IsPlaying && EmuApi.IsPaused()) {
				EmuApi.Resume();
			}
		}

		public void UpdateFrameCount()
		{
			TimingInfo timing = EmuApi.GetTimingInfo(CpuType);
			CurrentFrame = (int)timing.FrameCount;
		}
	}
}
