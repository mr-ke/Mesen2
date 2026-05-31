using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Mesen.Utilities;
using System;
using System.Threading.Tasks;

namespace Mesen.Windows
{
	public partial class CountdownMessageBox : MesenWindow
	{
		private DispatcherTimer? _countdownTimer;
		private int _remainingSeconds;
		private TaskCompletionSource<bool> _tcs;

		public CountdownMessageBox()
		{
			InitializeComponent();
#if DEBUG
			this.AttachDevTools();
#endif
			_tcs = new TaskCompletionSource<bool>();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		public static Task<bool> Show(Window? parent, string text, string title, int countdownSeconds)
		{
			CountdownMessageBox msgbox = new CountdownMessageBox() { Title = title };
			msgbox._remainingSeconds = countdownSeconds;
			
			msgbox.GetControl<TextBlock>("txtMessage").Text = text;
			msgbox.UpdateCountdownText();

			Button btnYes = msgbox.GetControl<Button>("btnYes");
			Button btnNo = msgbox.GetControl<Button>("btnNo");

			btnYes.Click += (_, _) => {
				msgbox.CloseDialog(true);
			};

			btnNo.Click += (_, _) => {
				msgbox.CloseDialog(false);
			};

			msgbox._countdownTimer = new DispatcherTimer(TimeSpan.FromSeconds(1), DispatcherPriority.Normal, (_, _) => {
				msgbox._remainingSeconds--;
				if(msgbox._remainingSeconds <= 0) {
					msgbox.CloseDialog(false);
				} else {
					msgbox.UpdateCountdownText();
				}
			});

			parent ??= ApplicationHelper.GetActiveOrMainWindow();

			if(parent != null) {
				if(!OperatingSystem.IsWindows()) {
					msgbox.Opened += (_, _) => { WindowExtensions.CenterWindow(msgbox, parent); };
				}

				msgbox.WindowStartupLocation = WindowStartupLocation.CenterOwner;
				msgbox.ShowDialog(parent);
			}

			return msgbox._tcs.Task;
		}

		private void UpdateCountdownText()
		{
			this.GetControl<TextBlock>("txtCountdown").Text = $"Auto-close in {_remainingSeconds} seconds...";
		}

		private void CloseDialog(bool result)
		{
			_countdownTimer?.Stop();
			_tcs.TrySetResult(result);
			Close();
		}
	}
}
