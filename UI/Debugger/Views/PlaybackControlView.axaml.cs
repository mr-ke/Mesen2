using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Mesen.Debugger.ViewModels;
using Mesen.Interop;

namespace Mesen.Debugger.Views
{
	public partial class PlaybackControlView : UserControl
	{
		public PlaybackControlView()
		{
			InitializeComponent();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		private void OnPlayTAS(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.PlayTAS();
			}
		}

		private void OnStopTAS(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.StopTAS();
			}
		}

		private void OnPreviousMarker(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.PreviousMarker();
			}
		}

		private void OnRewindFrame(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.RewindFrame();
			}
		}

		private void OnTogglePause(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.TogglePause();
			}
		}

		private void OnAdvanceFrame(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.AdvanceFrame();
			}
		}

		private void OnNextMarker(object sender, RoutedEventArgs e)
		{
			if(DataContext is PlaybackControlViewModel vm) {
				vm.NextMarker();
			}
		}
	}
}
