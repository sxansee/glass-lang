// Мини-разметка текста уроков (теория, условие задачи): абзацы, заголовки, списки, примечания, блоки кода.
// Намеренно простая — только то, что нужно уроку, и разбирается в переносимом коде (тестируется без WPF).
//
//   # Заголовок              — заголовок раздела
//   > Примечание             — выделенная врезка
//   - пункт списка           — маркированный список
//   ```glass ... ```         — блок кода (язык — после ```; «output» — вывод программы)
//   пустая строка            — граница абзацев
// В тексте: **жирный** и `код`.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Glass.Studio.Course
{
    public sealed record InlineSpan(string Text, bool IsCode, bool IsBold);

    public sealed class RichText
    {
        public RichText(IReadOnlyList<InlineSpan> spans)
        {
            Spans = spans;
        }

        public IReadOnlyList<InlineSpan> Spans { get; }

        public string PlainText => string.Concat(Spans.Select(s => s.Text));

        public static RichText Parse(string markup)
        {
            var spans = new List<InlineSpan>();
            var current = new StringBuilder();
            bool bold = false, code = false;

            void Flush()
            {
                if (current.Length == 0) return;
                spans.Add(new InlineSpan(current.ToString(), code, bold));
                current.Clear();
            }

            for (int i = 0; i < markup.Length; i++)
            {
                char c = markup[i];
                if (c == '`' && (code || markup.IndexOf('`', i + 1) > i))
                {
                    Flush();
                    code = !code;
                }
                else if (!code && c == '*' && i + 1 < markup.Length && markup[i + 1] == '*' &&
                         (bold || markup.IndexOf("**", i + 2, StringComparison.Ordinal) > i))
                {
                    Flush();
                    bold = !bold;
                    i++;
                }
                else
                {
                    current.Append(c);
                }
            }
            Flush();
            return new RichText(spans);
        }
    }

    public abstract class ContentBlock
    {
    }

    public sealed class HeadingBlock : ContentBlock
    {
        public HeadingBlock(string text) { Text = text; }
        public string Text { get; }
    }

    public sealed class ParagraphBlock : ContentBlock
    {
        public ParagraphBlock(RichText text) { Text = text; }
        public RichText Text { get; }
    }

    public sealed class NoteBlock : ContentBlock
    {
        public NoteBlock(RichText text) { Text = text; }
        public RichText Text { get; }
    }

    public sealed class BulletBlock : ContentBlock
    {
        public BulletBlock(IReadOnlyList<RichText> items) { Items = items; }
        public IReadOnlyList<RichText> Items { get; }
    }

    public sealed class CodeBlock : ContentBlock
    {
        public CodeBlock(string code, string language)
        {
            Code = code;
            Language = language;
        }

        public string Code { get; }

        /// <summary>«glass» — программа/фрагмент на Glass; «output» — вывод; пусто — просто текст.</summary>
        public string Language { get; }

        public bool IsOutput => Language == "output";
    }

    public static class LessonMarkup
    {
        public static IReadOnlyList<ContentBlock> Parse(string text)
        {
            var blocks = new List<ContentBlock>();
            string[] lines = text.Replace("\r\n", "\n").Split('\n');
            var paragraph = new List<string>();
            var note = new List<string>();
            var bullets = new List<StringBuilder>();

            void FlushParagraph()
            {
                if (paragraph.Count == 0) return;
                blocks.Add(new ParagraphBlock(RichText.Parse(string.Join(" ", paragraph))));
                paragraph.Clear();
            }
            void FlushNote()
            {
                if (note.Count == 0) return;
                blocks.Add(new NoteBlock(RichText.Parse(string.Join(" ", note))));
                note.Clear();
            }
            void FlushBullets()
            {
                if (bullets.Count == 0) return;
                blocks.Add(new BulletBlock(bullets.Select(b => RichText.Parse(b.ToString())).ToList()));
                bullets.Clear();
            }
            void FlushAll()
            {
                FlushParagraph();
                FlushNote();
                FlushBullets();
            }

            for (int i = 0; i < lines.Length; i++)
            {
                string line = lines[i];
                string trimmed = line.Trim();

                if (trimmed.StartsWith("```", StringComparison.Ordinal))
                {
                    FlushAll();
                    string language = trimmed.Substring(3).Trim();
                    var code = new List<string>();
                    for (i++; i < lines.Length && lines[i].Trim() != "```"; i++) code.Add(lines[i]);
                    blocks.Add(new CodeBlock(string.Join("\n", code), language));
                    continue;
                }
                if (trimmed.Length == 0)
                {
                    FlushAll();
                    continue;
                }
                if (trimmed.StartsWith("# ", StringComparison.Ordinal))
                {
                    FlushAll();
                    blocks.Add(new HeadingBlock(trimmed.Substring(2).Trim()));
                    continue;
                }
                if (trimmed.StartsWith("> ", StringComparison.Ordinal))
                {
                    FlushParagraph();
                    FlushBullets();
                    note.Add(trimmed.Substring(2).Trim());
                    continue;
                }
                if (trimmed.StartsWith("- ", StringComparison.Ordinal))
                {
                    FlushParagraph();
                    FlushNote();
                    bullets.Add(new StringBuilder(trimmed.Substring(2).Trim()));
                    continue;
                }
                if (bullets.Count > 0 && line.StartsWith("  ", StringComparison.Ordinal))
                {
                    bullets[bullets.Count - 1].Append(' ').Append(trimmed);   // продолжение пункта списка
                    continue;
                }
                FlushNote();
                FlushBullets();
                paragraph.Add(trimmed);
            }
            FlushAll();
            return blocks;
        }
    }
}
