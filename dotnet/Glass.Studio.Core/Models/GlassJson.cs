// Разбор JSON из glass_capi (формы описаны в GlassApi.h) в модели. System.Text.Json
// входит в .NET — отдельных зависимостей нет. Все методы терпимы к пустому
// вводу, но НЕ глотают настоящий мусор: неверная форма — JsonException.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Glass.Studio.Models
{
    public static class GlassJson
    {
        public static IReadOnlyList<StackFrameInfo> ParseCallStack(string json)
        {
            using JsonDocument doc = JsonDocument.Parse(json);
            var frames = new List<StackFrameInfo>();
            foreach (JsonElement item in doc.RootElement.EnumerateArray())
            {
                frames.Add(new StackFrameInfo(
                    item.GetProperty("function").GetString() ?? "",
                    item.GetProperty("line").GetInt32()));
            }
            return frames;
        }

        public static IReadOnlyList<LocalVariable> ParseLocals(string json)
        {
            using JsonDocument doc = JsonDocument.Parse(json);
            return ParseNamedValues(doc.RootElement);
        }

        public static HeapSnapshot ParseHeap(string json)
        {
            using JsonDocument doc = JsonDocument.Parse(json);
            var objects = new List<HeapObject>();
            foreach (JsonElement item in doc.RootElement.GetProperty("objects").EnumerateArray())
            {
                string address = item.GetProperty("address").GetString() ?? "";
                bool reachable = !item.TryGetProperty("reachable", out JsonElement r) || r.ValueKind != JsonValueKind.False;
                string kind = item.GetProperty("kind").GetString() ?? "object";
                if (kind == "array")
                {
                    var elements = new List<LocalVariable>();
                    int index = 0;
                    foreach (JsonElement e in item.GetProperty("elements").EnumerateArray())
                    {
                        elements.Add(new LocalVariable("[" + index.ToString(CultureInfo.InvariantCulture) + "]", ParseValue(e)));
                        index++;
                    }
                    objects.Add(new HeapObject(address, true, "[]", reachable, elements));
                }
                else
                {
                    objects.Add(new HeapObject(
                        address, false, item.GetProperty("class").GetString() ?? "?", reachable,
                        ParseNamedValues(item.GetProperty("fields"))));
                }
            }
            return new HeapSnapshot(objects);
        }

        /// <summary>null (JSON null или пустая строка) — нет такого живого объекта.</summary>
        public static ObjectInfo? ParseObjectInfo(string? json)
        {
            if (string.IsNullOrWhiteSpace(json)) return null;
            using JsonDocument doc = JsonDocument.Parse(json);
            JsonElement root = doc.RootElement;
            if (root.ValueKind == JsonValueKind.Null) return null;

            string address = root.GetProperty("address").GetString() ?? "";
            if (root.GetProperty("kind").GetString() == "array")
            {
                return new ObjectInfo(address, true, "[]", root.GetProperty("length").GetInt32(),
                    Array.Empty<string>(), Array.Empty<FieldLayout>(), Array.Empty<VTableEntry>());
            }

            var ancestry = new List<string>();
            foreach (JsonElement a in root.GetProperty("ancestry").EnumerateArray()) ancestry.Add(a.GetString() ?? "");
            var layout = new List<FieldLayout>();
            foreach (JsonElement f in root.GetProperty("layout").EnumerateArray())
            {
                layout.Add(new FieldLayout(
                    f.GetProperty("name").GetString() ?? "", f.GetProperty("offset").GetInt32(),
                    f.GetProperty("size").GetInt32(), f.GetProperty("type").GetString() ?? "ref"));
            }
            var vtable = new List<VTableEntry>();
            foreach (JsonElement v in root.GetProperty("vtable").EnumerateArray())
            {
                vtable.Add(new VTableEntry(
                    v.GetProperty("method").GetString() ?? "", v.GetProperty("implementedBy").GetString() ?? ""));
            }
            return new ObjectInfo(address, false, root.GetProperty("class").GetString() ?? "?", 0, ancestry, layout, vtable);
        }

        private static List<LocalVariable> ParseNamedValues(JsonElement array)
        {
            var result = new List<LocalVariable>();
            foreach (JsonElement item in array.EnumerateArray())
            {
                result.Add(new LocalVariable(item.GetProperty("name").GetString() ?? "", ParseValue(item.GetProperty("value"))));
            }
            return result;
        }

        internal static GlassValue ParseValue(JsonElement e)
        {
            switch (e.ValueKind)
            {
                case JsonValueKind.Null: return GlassValue.Null;
                case JsonValueKind.True: return GlassValue.FromBool(true);
                case JsonValueKind.False: return GlassValue.FromBool(false);
                case JsonValueKind.String: return GlassValue.FromString(e.GetString() ?? "");
                case JsonValueKind.Number:
                    return e.TryGetInt64(out long l) ? GlassValue.FromInt(l) : GlassValue.FromDouble(e.GetRawText());
                case JsonValueKind.Object:
                    if (e.TryGetProperty("ref", out JsonElement r))
                    {
                        string address = r.GetString() ?? "";
                        return e.GetProperty("kind").GetString() == "array"
                            ? GlassValue.ArrayRef(address, e.GetProperty("length").GetInt32())
                            : GlassValue.ObjectRef(address, e.GetProperty("class").GetString() ?? "?");
                    }
                    if (e.TryGetProperty("kind", out JsonElement k) && k.GetString() == "double")
                    {
                        return GlassValue.FromDouble(e.GetProperty("text").GetString() ?? "nan");  // inf/nan
                    }
                    return GlassValue.Null;
                default:
                    return GlassValue.Null;
            }
        }
    }

    /// <summary>
    /// Ошибки приходят строками: компиляция — "СТРОКА:СТОЛБЕЦ: сообщение",
    /// выполнение — "функция:СТРОКА: сообщение". Достаём позицию, чтобы
    /// редактор мог подчеркнуть нужную строку; не распознали — позиция 0.
    /// </summary>
    public static class DiagnosticParser
    {
        private static readonly Regex CompilePattern = new Regex(@"^(?<line>\d+):(?<col>\d+):\s*(?<msg>.*)$", RegexOptions.Singleline);
        private static readonly Regex RuntimePattern = new Regex(@"^(?<fn>[^:\s]+):(?<line>\d+):\s*(?<msg>.*)$", RegexOptions.Singleline);

        public static Diagnostic ParseCompileError(string text)
        {
            Match m = CompilePattern.Match(text);
            if (!m.Success) return new Diagnostic(DiagnosticKind.Compile, 0, 0, text);
            return new Diagnostic(DiagnosticKind.Compile,
                int.Parse(m.Groups["line"].Value, CultureInfo.InvariantCulture),
                int.Parse(m.Groups["col"].Value, CultureInfo.InvariantCulture),
                m.Groups["msg"].Value);
        }

        public static Diagnostic ParseRuntimeError(string text)
        {
            Match m = RuntimePattern.Match(text);
            if (!m.Success) return new Diagnostic(DiagnosticKind.Runtime, 0, 0, text);
            return new Diagnostic(DiagnosticKind.Runtime,
                int.Parse(m.Groups["line"].Value, CultureInfo.InvariantCulture), 0, m.Groups["msg"].Value);
        }
    }
}
