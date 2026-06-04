using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Mesen.Utilities
{
	public class ShaderPresetInfo
	{
		public string Name { get; set; } = "";
		public string RelativePath { get; set; } = "";
		public string FullPath { get; set; } = "";

		public override string ToString() => Name;
	}

	public static class ShaderHelper
	{
		private static List<ShaderPresetInfo>? _shaderPresets = null;

		public static List<ShaderPresetInfo> GetShaderPresets()
		{
			if(_shaderPresets != null) {
				return _shaderPresets;
			}

			_shaderPresets = new List<ShaderPresetInfo>();

			// Add "None" option
			_shaderPresets.Add(new ShaderPresetInfo {
				Name = "None",
				RelativePath = "",
				FullPath = ""
			});

			try {
				string shadersPath = GetShadersPath();
				if(Directory.Exists(shadersPath)) {
					ScanShaderDirectory(shadersPath, "", _shaderPresets);
				}
			} catch {
				// Ignore errors
			}

			return _shaderPresets;
		}

		private static void ScanShaderDirectory(string basePath, string relativePath, List<ShaderPresetInfo> presets)
		{
			// Get all .slangp files in current directory
			foreach(string file in Directory.GetFiles(basePath, "*.slangp")) {
				string fileName = Path.GetFileNameWithoutExtension(file);
				string relPath = string.IsNullOrEmpty(relativePath) 
					? Path.GetFileName(file) 
					: Path.Combine(relativePath, Path.GetFileName(file));

				presets.Add(new ShaderPresetInfo {
					Name = string.IsNullOrEmpty(relativePath) ? fileName : $"{relativePath}/{fileName}",
					RelativePath = relPath,
					FullPath = file
				});
			}

			// Recursively scan subdirectories
			foreach(string dir in Directory.GetDirectories(basePath)) {
				string dirName = Path.GetFileName(dir);
				string newRelativePath = string.IsNullOrEmpty(relativePath) ? dirName : Path.Combine(relativePath, dirName);
				ScanShaderDirectory(dir, newRelativePath, presets);
			}
		}

		public static string GetShadersPath()
		{
			// Get the directory where the executable is located
			string exePath = AppContext.BaseDirectory;
			return Path.Combine(exePath, "Shaders");
		}

		public static void ClearCache()
		{
			_shaderPresets = null;
		}
	}
}
