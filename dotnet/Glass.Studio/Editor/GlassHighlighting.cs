using System;
using System.IO;
using System.Reflection;
using System.Xml;
using ICSharpCode.AvalonEdit.Highlighting;
using ICSharpCode.AvalonEdit.Highlighting.Xshd;

namespace Glass.Studio.Editor
{
    /// <summary>Подсветка синтаксиса Glass: правила в Editor/Glass.xshd (встроенный ресурс).</summary>
    internal static class GlassHighlighting
    {
        private const string ResourceName = "Glass.Studio.Editor.Glass.xshd";

        public static IHighlightingDefinition? Load()
        {
            try
            {
                Assembly assembly = typeof(GlassHighlighting).Assembly;
                using Stream? stream = assembly.GetManifestResourceStream(ResourceName);
                if (stream == null) return null;
                using XmlReader reader = XmlReader.Create(stream);
                return HighlightingLoader.Load(reader, HighlightingManager.Instance);
            }
            catch (Exception ex) when (ex is XmlException || ex is HighlightingDefinitionInvalidException || ex is InvalidOperationException)
            {
                // Редактор без подсветки лучше, чем IDE, которая не запускается из-за опечатки в .xshd.
                return null;
            }
        }
    }
}
