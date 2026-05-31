using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace Mesen.Debugger.Utilities
{
	[JsonSerializable(typeof(List<BookmarkData>))]
	[JsonSourceGenerationOptions(WriteIndented = true)]
	public partial class BookmarkDataJsonContext : JsonSerializerContext
	{
	}
}
