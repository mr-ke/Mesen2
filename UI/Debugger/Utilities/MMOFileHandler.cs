using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;
using Mesen.Interop;

namespace Mesen.Debugger.Utilities
{
	public class MMOFileHandler
	{
		private const string InputFileName = "Input.txt";
		private const string GameSettingsFileName = "GameSettings.txt";

		public static List<TASInputFrame> ImportMMO(string mmoFilePath)
		{
			List<TASInputFrame> frames = new List<TASInputFrame>();

			using(FileStream zipStream = new FileStream(mmoFilePath, FileMode.Open, FileAccess.Read))
			using(ZipArchive archive = new ZipArchive(zipStream, ZipArchiveMode.Read)) {
				ZipArchiveEntry? inputEntry = archive.GetEntry(InputFileName);
				if(inputEntry == null) {
					throw new FileNotFoundException($"'{InputFileName}' not found in MMO file");
				}

				using(Stream inputSream = inputEntry.Open())
				using(StreamReader reader = new StreamReader(inputSream)) {
					string? line;
					while((line = reader.ReadLine()) != null) {
						if(line.StartsWith("|")) {
							TASInputFrame frame = ParseInputLine(line);
							frames.Add(frame);
						}
					}
				}
			}

			return frames;
		}

		private static TASInputFrame ParseInputLine(string line)
		{
			TASInputFrame frame = new TASInputFrame();
			
			string[] deviceStates = line.Substring(1).Split('|');
			
			if(deviceStates.Length > 1) {
				string controllerState = deviceStates[1];
				ParseControllerState(controllerState, frame);
			}

			return frame;
		}

		private static void ParseControllerState(string state, TASInputFrame frame)
		{
			if(string.IsNullOrEmpty(state)) {
				return;
			}
			
			if(state.Length >= 8) {
				frame.Up = state[0] == 'U' ? "U" : ".";
				frame.Down = state[1] == 'D' ? "D" : ".";
				frame.Left = state[2] == 'L' ? "L" : ".";
				frame.Right = state[3] == 'R' ? "R" : ".";
				frame.Select = state[4] == 'S' ? "S" : ".";
				frame.Start = state[5] == 's' || state[5] == 'T' ? (state[5] == 's' ? "S" : "T") : ".";
				frame.ButtonB = state[6] == 'B' ? "B" : ".";
				frame.ButtonA = state[7] == 'A' ? "A" : ".";
			}

			if(state.Length >= 12) {
				frame.ButtonX = state[2] == 'X' ? "X" : ".";
				frame.ButtonY = state[3] == 'Y' ? "Y" : ".";
				frame.ButtonL = state[4] == 'L' ? "L" : ".";
				frame.ButtonR = state[5] == 'R' ? "R" : ".";
				frame.Select = state[6] == 'S' ? "S" : ".";
				frame.Start = state[7] == 'T' ? "T" : ".";
				frame.Up = state[8] == 'U' ? "U" : ".";
				frame.Down = state[9] == 'D' ? "D" : ".";
				frame.Left = state[10] == 'L' ? "L" : ".";
				frame.Right = state[11] == 'R' ? "R" : ".";
			}
		}

		public static void ExportMMO(string mmoFilePath, List<TASInputFrame> frames, string? originalMMOPath = null)
		{
			using(FileStream zipStream = new FileStream(mmoFilePath, FileMode.Create, FileAccess.Write))
			using(ZipArchive archive = new ZipArchive(zipStream, ZipArchiveMode.Create)) {
				ZipArchiveEntry inputEntry = archive.CreateEntry(InputFileName);
				using(Stream inputStream = inputEntry.Open())
				using(StreamWriter writer = new StreamWriter(inputStream)) {
					foreach(TASInputFrame frame in frames) {
						string line = FormatInputLine(frame);
						writer.WriteLine(line);
					}
				}

				if(!string.IsNullOrEmpty(originalMMOPath) && File.Exists(originalMMOPath)) {
					CopyGameSettings(originalMMOPath, archive);
				}
			}
		}

		private static string FormatInputLine(TASInputFrame frame)
		{
			StringBuilder sb = new StringBuilder();
			sb.Append("|..|");

			sb.Append(frame.Up == "U" ? "U" : ".");
			sb.Append(frame.Down == "D" ? "D" : ".");
			sb.Append(frame.Left == "L" ? "L" : ".");
			sb.Append(frame.Right == "R" ? "R" : ".");
			sb.Append(frame.Select == "S" ? "S" : ".");
			sb.Append(frame.Start == "S" || frame.Start == "T" ? (frame.Start == "S" ? "s" : "T") : ".");
			sb.Append(frame.ButtonB == "B" ? "B" : ".");
			sb.Append(frame.ButtonA == "A" ? "A" : ".");

			return sb.ToString();
		}

		private static void CopyGameSettings(string originalMMOPath, ZipArchive newArchive)
		{
			try {
				using(FileStream originalStream = new FileStream(originalMMOPath, FileMode.Open, FileAccess.Read))
				using(ZipArchive originalArchive = new ZipArchive(originalStream, ZipArchiveMode.Read)) {
					ZipArchiveEntry? settingsEntry = originalArchive.GetEntry(GameSettingsFileName);
					if(settingsEntry != null) {
						ZipArchiveEntry newSettingsEntry = newArchive.CreateEntry(GameSettingsFileName);
						using(Stream originalSettingsStream = settingsEntry.Open())
						using(Stream newSettingsStream = newSettingsEntry.Open())
						using(StreamReader reader = new StreamReader(originalSettingsStream))
						using(StreamWriter writer = new StreamWriter(newSettingsStream)) {
							writer.Write(reader.ReadToEnd());
						}
					}
				}
			} catch {
			}
		}
	}

	public class TASInputFrame
	{
		public string ButtonA { get; set; } = ".";
		public string ButtonB { get; set; } = ".";
		public string ButtonX { get; set; } = ".";
		public string ButtonY { get; set; } = ".";
		public string ButtonL { get; set; } = ".";
		public string ButtonR { get; set; } = ".";
		public string Select { get; set; } = ".";
		public string Start { get; set; } = ".";
		public string Up { get; set; } = ".";
		public string Down { get; set; } = ".";
		public string Left { get; set; } = ".";
		public string Right { get; set; } = ".";
	}
}
