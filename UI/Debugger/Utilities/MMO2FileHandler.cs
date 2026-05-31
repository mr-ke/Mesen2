using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text.Json;
using Mesen.Debugger.ViewModels;

namespace Mesen.Debugger.Utilities
{
	public class MMO2FileHandler : MMOFileHandler
	{
		private const string BookmarksFileName = "Bookmarks.json";

		public static TASProjectData ImportMMO2(string mmo2FilePath)
		{
			TASProjectData projectData = new TASProjectData();

			using(FileStream zipStream = new FileStream(mmo2FilePath, FileMode.Open, FileAccess.Read))
			using(ZipArchive archive = new ZipArchive(zipStream, ZipArchiveMode.Read)) {
				projectData.InputFrames = ReadInputData(archive);

				ReadBookmarks(archive, projectData);
			}

			return projectData;
		}

		private static List<TASInputFrame> ReadInputData(ZipArchive archive)
		{
			List<TASInputFrame> frames = new List<TASInputFrame>();
			
			ZipArchiveEntry? inputEntry = archive.GetEntry(InputFileName);
			if(inputEntry == null) {
				throw new FileNotFoundException($"'{InputFileName}' not found in MMO2 file");
			}

			using(Stream inputStream = inputEntry.Open())
			using(StreamReader reader = new StreamReader(inputStream)) {
				string? line;
				while((line = reader.ReadLine()) != null) {
					if(line.StartsWith("|")) {
						TASInputFrame frame = ParseInputLine(line);
						frames.Add(frame);
					}
				}
			}

			return frames;
		}

		private static void ReadBookmarks(ZipArchive archive, TASProjectData projectData)
		{
			ZipArchiveEntry? bookmarksEntry = archive.GetEntry(BookmarksFileName);
			if(bookmarksEntry != null) {
				using(Stream bookmarksStream = bookmarksEntry.Open())
				using(StreamReader reader = new StreamReader(bookmarksStream)) {
					string json = reader.ReadToEnd();
					try {
						projectData.Bookmarks = JsonSerializer.Deserialize(json, BookmarkDataJsonContext.Default.ListBookmarkData) ?? new List<BookmarkData>();
					} catch {
						projectData.Bookmarks = new List<BookmarkData>();
					}
				}

				for(int i = 0; i < projectData.Bookmarks.Count; i++) {
					string savestateFileName = $"Savestate_{i}.sav";
					ZipArchiveEntry? savestateEntry = archive.GetEntry(savestateFileName);
					if(savestateEntry != null) {
						using(Stream savestateStream = savestateEntry.Open())
						using(MemoryStream ms = new MemoryStream()) {
							savestateStream.CopyTo(ms);
							projectData.Bookmarks[i].SavestateData = ms.ToArray();
						}
					}
				}
			}
		}

		public static void ExportMMO2(string mmo2FilePath, TASProjectData projectData, string? originalMMOPath = null)
		{
			using(FileStream zipStream = new FileStream(mmo2FilePath, FileMode.Create, FileAccess.Write))
			using(ZipArchive archive = new ZipArchive(zipStream, ZipArchiveMode.Create)) {
				WriteInputData(archive, projectData.InputFrames);

				if(projectData.Bookmarks.Count > 0) {
					WriteBookmarks(archive, projectData.Bookmarks);
				}

				if(!string.IsNullOrEmpty(originalMMOPath) && File.Exists(originalMMOPath)) {
					CopyGameSettings(originalMMOPath, archive);
				}
			}
		}

		private static void WriteBookmarks(ZipArchive archive, List<BookmarkData> bookmarks)
		{
			List<BookmarkData> bookmarksMeta = new List<BookmarkData>();
			for(int i = 0; i < bookmarks.Count; i++) {
				BookmarkData bookmark = bookmarks[i];
				BookmarkData meta = new BookmarkData {
					SlotNumber = bookmark.SlotNumber,
					Alias = bookmark.Alias,
					FrameNumber = bookmark.FrameNumber,
					TimeStamp = bookmark.TimeStamp,
					HasData = bookmark.HasData
				};
				bookmarksMeta.Add(meta);

				if(bookmark.SavestateData != null && bookmark.SavestateData.Length > 0) {
					string savestateFileName = $"Savestate_{i}.sav";
					ZipArchiveEntry savestateEntry = archive.CreateEntry(savestateFileName);
					using(Stream savestateStream = savestateEntry.Open()) {
						savestateStream.Write(bookmark.SavestateData, 0, bookmark.SavestateData.Length);
					}
				}
			}

			string json = JsonSerializer.Serialize(bookmarksMeta, BookmarkDataJsonContext.Default.ListBookmarkData);
			ZipArchiveEntry bookmarksEntry = archive.CreateEntry(BookmarksFileName);
			using(Stream bookmarksStream = bookmarksEntry.Open())
			using(StreamWriter writer = new StreamWriter(bookmarksStream)) {
				writer.Write(json);
			}
		}
	}

	public class TASProjectData
	{
		public List<TASInputFrame> InputFrames { get; set; } = new List<TASInputFrame>();
		public List<BookmarkData> Bookmarks { get; set; } = new List<BookmarkData>();
	}

	public class BookmarkData
	{
		public int SlotNumber { get; set; }
		public string Alias { get; set; } = "";
		public int FrameNumber { get; set; }
		public string TimeStamp { get; set; } = "--:--.---";
		public bool HasData { get; set; }
		public byte[]? SavestateData { get; set; }
	}
}
