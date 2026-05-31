using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using System.Threading.Tasks;

namespace Mesen.Windows
{
	public partial class InputDialog : MesenWindow
	{
		private TextBox? _textBox;
		private string? _result;

		public InputDialog()
		{
			InitializeComponent();
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
			_textBox = this.GetControl<TextBox>("InputTextBox");
		}

		public static async Task<string?> Show(Window? parent, string title, string prompt, string defaultValue = "")
		{
			InputDialog dialog = new InputDialog {
				Title = title
			};
			
			dialog._textBox!.Text = defaultValue;
			dialog.GetControl<TextBlock>("PromptText").Text = prompt;

			var okButton = dialog.GetControl<Button>("OkButton");
			var cancelButton = dialog.GetControl<Button>("CancelButton");

			okButton.Click += (_, _) => {
				dialog._result = dialog._textBox.Text;
				dialog.Close();
			};

			cancelButton.Click += (_, _) => {
				dialog._result = null;
				dialog.Close();
			};

			dialog._textBox.KeyDown += (_, e) => {
				if(e.Key == Avalonia.Input.Key.Enter) {
					dialog._result = dialog._textBox.Text;
					dialog.Close();
				}
			};

			TaskCompletionSource<string?> tcs = new TaskCompletionSource<string?>();
			dialog.Closed += (_, _) => { tcs.TrySetResult(dialog._result); };

			if(parent != null) {
				dialog.WindowStartupLocation = WindowStartupLocation.CenterOwner;
				dialog.ShowDialog(parent);
			} else {
				dialog.Show();
			}

			return await tcs.Task;
		}
	}
}
