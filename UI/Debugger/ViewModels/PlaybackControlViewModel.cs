using Avalonia.Collections;
using Mesen.Config;
using Mesen.Debugger.Utilities;
using Mesen.Debugger.Windows;
using Mesen.Interop;
using Mesen.Utilities;
using Mesen.ViewModels;
using ReactiveUI;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.IO.Compression;
using System.Reactive.Linq;
using System.Text;

namespace Mesen.Debugger.ViewModels
{
	public class PlaybackControlViewModel : DisposableViewModel
	{
		[Reactive] public bool IsPaused { get; private set; } = false;
		[Reactive] public int CurrentFrame { get; private set; } = 0;
		[Reactive] public int TotalFrames { get; private set; } = 100;
		[Reactive] public bool IsPlaying { get; private set; } = false;
		[Reactive] public bool FollowCursor { get; set; } = true;
		[Reactive] public double SeekSpeed { get; set; } = 1.0;

		public CpuType CpuType { get; }
		public TASEditorWindowViewModel TASEditor { get; }

		[Obsolete("For designer only")]
		public PlaybackControlViewModel() : this(CpuType.Snes, new()) { }

		public PlaybackControlViewModel(CpuType cpuType, TASEditorWindowViewModel tasEditor)
		{
			CpuType = cpuType;
			TASEditor = tasEditor;

			FollowCursor = ConfigManager.Config.Debug.Debugger.TASFollowCursor;
			SeekSpeed = ConfigManager.Config.Debug.Debugger.TASSeekSpeed;

			this.WhenAnyValue(x => x.FollowCursor)
				.BindTo(ConfigManager.Config.Debug.Debugger, x => x.TASFollowCursor);

			this.WhenAnyValue(x => x.SeekSpeed)
				.Do(speed => {
					ConfigManager.Config.Emulation.EmulationSpeed = (uint)(speed * 100);
					ConfigManager.Config.Emulation.ApplyConfig();
				})
				.BindTo(ConfigManager.Config.Debug.Debugger, x => x.TASSeekSpeed);
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
			if(IsPlaying) {
				RecordApi.MovieStop();
				EmuApi.Pause();
				IsPlaying = false;
				IsPaused = true;
				UpdateFrameCount();
				TASEditor.FrameList.SelectFrame(CurrentFrame);
			} else {
				IsPaused = !IsPaused;
				if(IsPaused) {
					EmuApi.Pause();
					UpdateFrameCount();
					TASEditor.FrameList.SelectFrame(CurrentFrame);
				} else {
					EmuApi.Resume();
				}
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
				if(IsPlaying) {
					RecordApi.MovieStop();
					EmuApi.Pause();
					IsPlaying = false;
					IsPaused = true;
					UpdateFrameCount();
					TASEditor.FrameList.SelectFrame(CurrentFrame);
				} else {
					if(IsPaused) {
						EmuApi.Resume();
						IsPaused = false;
					}

					string tempMMOPath = Path.Combine(Path.GetTempPath(), "MesenTAS_" + Guid.NewGuid().ToString() + ".mmo");
					TASEditor.FrameList.ExportMMO(tempMMOPath);
					
					RecordApi.MoviePlay(tempMMOPath);
					IsPlaying = true;
				}
			} catch(Exception ex) {
				System.Diagnostics.Debug.WriteLine($"Play TAS failed: {ex.Message}");
			}
		}

		public void PlayFromFrame(int frameIndex)
		{
			if(!TASEditor.FrameList.HasImportedData || frameIndex < 0 || frameIndex >= TASEditor.FrameList.Frames.Count) {
				return;
			}

			System.Diagnostics.Debug.WriteLine($"PlayFromFrame({frameIndex}) - Will be implemented with Bookmark functionality");
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
			
			if(IsPlaying && FollowCursor) {
				TASEditor.FrameList.SelectFrame(CurrentFrame);
			}
		}
	}
}
