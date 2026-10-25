using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Glass.Interop;
using Glass.Studio.CodeInfo;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// Документация проверяется так же, как код: примеры на страницах исполняются настоящей VM, справочник совпадает с
    /// записями, из которых IDE строит карточки, ссылки живы. Расхождение — красный тест, а не «устаревшая страница».
    ///
    /// Разметка примеров в docs/**/*.md:
    ///   ```glass run     программа; сразу после неё блок ```output с ожидаемым выводом
    ///   ```glass error   программа, которая обязана НЕ скомпилироваться; в ```output — сообщение компилятора
    ///   ```glass         просто иллюстрация, не исполняется
    /// В выводе «&lt;адрес&gt;» заменяет число, которое меняется от запуска к запуску (адрес объекта).
    ///
    /// Обновить файлы после изменения языка: GLASS_UPDATE_DOCS=1 dotnet run -c Release -- документация
    /// </summary>
    public static class DocsTests
    {
        public static void Register()
        {
            Add("документация: примеры на страницах исполняются настоящей VM, вывод совпадает", ExamplesRun);
            Add("документация актуальна: справочник языка совпадает с записями LanguageReference", ReferenceIsUpToDate);
            Add("документация: страницы карточек справочника существуют и упоминают свои записи", CardPagesExist);
            Add("документация: ссылки между страницами живы, оглавление перечисляет все страницы", LinksAreAlive);
        }

        private static bool UpdateMode => Environment.GetEnvironmentVariable("GLASS_UPDATE_DOCS") == "1";

        private static string? FindRoot()
        {
            for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                if (File.Exists(Path.Combine(dir.FullName, "CMakeLists.txt")) && Directory.Exists(Path.Combine(dir.FullName, "dotnet")))
                    return dir.FullName;
            }
            return null;
        }

        private static string DocsDir => Path.Combine(FindRoot() ?? throw new InvalidOperationException("не найден корень репозитория"), "docs");

        private static string ReadText(string path) => File.ReadAllText(path).Replace("\r\n", "\n");

        private static IEnumerable<string> Pages() =>
            Directory.EnumerateFiles(DocsDir, "*.md", SearchOption.AllDirectories).OrderBy(p => p, StringComparer.Ordinal);

        private static string Rel(string path) => Path.GetRelativePath(DocsDir, path).Replace('\\', '/');

        // ------------------------------------------------------------------ примеры

        private static readonly Regex ExampleBlock = new Regex(
            @"```glass (?<kind>run|error)\n(?<code>.*?)```\n\s*```output\n(?<out>.*?)```", RegexOptions.Singleline);
        private static readonly Regex AnyExampleFence = new Regex(@"```glass (run|error)\n");

        private static string Normalize(string text)
        {
            text = Regex.Replace(text, "0x[0-9a-fA-F]{6,}", "<адрес>");
            return Regex.Replace(text, @"@\d{6,}", "@<адрес>");
        }

        private static string Execute(string kind, string code)
        {
            GlassSession session;
            try
            {
                session = GlassSession.Compile(code);
            }
            catch (GlassCompileException ex)
            {
                return kind == "error" ? ex.Message : "(не скомпилировалась) " + ex.Message;
            }

            using (session)
            {
                if (kind == "error") return "(ошибка не обнаружена: программа скомпилировалась)";
                bool ok = session.Run();
                string text = session.GetOutput().TrimEnd('\n');
                if (!ok) text += (text.Length > 0 ? "\n" : "") + "Ошибка выполнения: " + session.GetError();
                return text;
            }
        }

        private static List<string> Lines(string text) =>
            text.Replace("\r\n", "\n").Split('\n').Select(l => l.TrimEnd()).Reverse().SkipWhile(l => l.Length == 0).Reverse().ToList();

        private static bool LineMatches(string expected, string actual)
        {
            if (!expected.Contains("<адрес>", StringComparison.Ordinal)) return expected == actual;
            string pattern = "^" + Regex.Escape(expected).Replace("<адрес>", "(?:0x[0-9a-fA-F]+|[0-9]+)") + "$";
            return Regex.IsMatch(actual, pattern);
        }

        private static bool OutputMatches(string kind, string expected, string actual)
        {
            if (kind == "error")
            {
                // glassc печатает заголовок со счётчиком ошибок, C API — диагностику первой ошибки: сообщение должно содержаться в блоке.
                string collapse(string s) => Regex.Replace(s, @"\s+", " ").Trim();
                return collapse(expected).Contains(collapse(actual), StringComparison.Ordinal);
            }
            List<string> e = Lines(expected), a = Lines(actual);
            if (e.Count != a.Count) return false;
            for (int i = 0; i < e.Count; i++) if (!LineMatches(e[i], a[i])) return false;
            return true;
        }

        private static void ExamplesRun()
        {
            int total = 0;
            var perPage = new Dictionary<string, int>();
            foreach (string page in Pages())
            {
                string text = ReadText(page);
                string name = Rel(page);

                int fences = AnyExampleFence.Matches(text).Count;
                int blocks = ExampleBlock.Matches(text).Count;
                CheckEqual(fences, blocks, name + ": у каждого примера ```glass run / error есть следующий за ним блок ```output");

                string updated = ExampleBlock.Replace(text, m =>
                {
                    string kind = m.Groups["kind"].Value, code = m.Groups["code"].Value, expected = m.Groups["out"].Value;
                    string rawActual = Execute(kind, code);
                    string actual = Normalize(rawActual);   // для записи в файл и для показа; сравниваем СЫРОЙ вывод — «<адрес>» в ожидаемом блоке сам станет шаблоном
                    total++;
                    perPage[name] = perPage.GetValueOrDefault(name) + 1;
                    bool ok = OutputMatches(kind, expected, rawActual);
                    if (UpdateMode && kind == "run" && !ok)
                        return "```glass run\n" + code + "```\n\n```output\n" + actual + "\n```";
                    if (!ok)
                    {
                        string first = code.Split('\n').FirstOrDefault(l => l.TrimStart().StartsWith("print", StringComparison.Ordinal)) ?? code.Split('\n')[0];
                        Check(false, name + ": пример «" + first.Trim() + "…» дал другой вывод.\n  ожидалось:\n    " +
                                     string.Join("\n    ", Lines(expected)) + "\n  получено:\n    " + string.Join("\n    ", Lines(actual)));
                    }
                    else Check(true, name + ": пример совпал");
                    return m.Value;
                });

                if (UpdateMode && updated != text) File.WriteAllText(page, updated);
            }

            Check(total >= 40, "на страницах документации не меньше 40 исполняемых примеров (сейчас " + total + ")");
            foreach (string required in new[] { "language/basics.md", "language/control-flow.md", "language/functions.md", "language/strings.md",
                                                "language/arrays.md", "language/objects.md", "language/exceptions.md", "language/introspection.md" })
                Check(perPage.GetValueOrDefault(required) >= 2, required + ": есть исполняемые примеры (сейчас " + perPage.GetValueOrDefault(required) + ")");
        }

        // ------------------------------------------------------------------ справочник

        private static string ReferenceMarkdown()
        {
            var order = new (ReferenceKind Kind, string Title)[]
            {
                (ReferenceKind.Keyword, "Ключевые слова"), (ReferenceKind.Modifier, "Модификаторы"), (ReferenceKind.Type, "Типы данных"),
                (ReferenceKind.Literal, "Значения"), (ReferenceKind.Constant, "Встроенные константы"), (ReferenceKind.Function, "Встроенные функции"),
                (ReferenceKind.ArrayMethod, "Методы массивов"), (ReferenceKind.StringMethod, "Методы строк"),
                (ReferenceKind.Property, "Свойства"), (ReferenceKind.Introspection, "Операторы интроспекции"),
            };
            var md = new StringBuilder();
            md.Append("# Справочник языка Glass\n\n");
            md.Append("> Файл **генерируется** из `LanguageReference` — тех же записей, по которым IDE строит карточки элементов кода. ");
            md.Append("Не правьте его вручную: измените запись в `dotnet/Glass.Studio.Core/CodeInfo/LanguageReference.cs` и выполните ");
            md.Append("`GLASS_UPDATE_DOCS=1 dotnet run -c Release -- документация` (см. [сборку и тесты](../building.md)).\n\n");
            md.Append("Записей: ").Append(LanguageReference.All.Count).Append(". Подробные объяснения с исполняемыми примерами — на страницах языка из [оглавления](../README.md).\n\n");
            md.Append("## Содержание\n\n");
            foreach (var (kind, title) in order)
            {
                var names = LanguageReference.All.Where(e => e.Kind == kind).Select(e => "`" + e.Name + "`").ToList();
                if (names.Count > 0) md.Append("- [").Append(title).Append("](#").Append(title.ToLowerInvariant().Replace(' ', '-')).Append(") — ").Append(string.Join(", ", names)).Append('\n');
            }
            md.Append('\n');
            foreach (var (kind, title) in order)
            {
                var entries = LanguageReference.All.Where(e => e.Kind == kind).ToList();
                if (entries.Count == 0) continue;
                md.Append("## ").Append(title).Append("\n\n");
                foreach (ReferenceEntry e in entries)
                {
                    md.Append("### `").Append(e.Name).Append("`\n\n");
                    md.Append('*').Append(e.KindText).Append("*\n\n");
                    md.Append("```\n").Append(e.Signature).Append("\n```\n\n");
                    md.Append(e.Description).Append("\n\n");
                    if (!string.IsNullOrWhiteSpace(e.Example))
                        md.Append("Пример:\n\n```glass\n").Append(e.Example).Append("\n```\n\n");
                    if (e.DocTopic != "reference/language-reference.md")
                        md.Append("Подробнее: [").Append(e.DocTopic).Append("](../").Append(e.DocTopic).Append(")\n\n");
                }
            }
            return md.ToString().TrimEnd('\n') + "\n";
        }

        private static void ReferenceIsUpToDate()
        {
            string path = Path.Combine(DocsDir, "reference", "language-reference.md");
            string expected = ReferenceMarkdown();
            if (UpdateMode)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, expected);
            }
            Check(File.Exists(path), "docs/reference/language-reference.md существует");
            if (!File.Exists(path)) return;
            string actual = ReadText(path);
            bool same = actual == expected;
            if (!same)
            {
                string[] a = actual.Split('\n'), e = expected.Split('\n');
                int line = Enumerable.Range(0, Math.Min(a.Length, e.Length)).FirstOrDefault(i => a[i] != e[i], Math.Min(a.Length, e.Length));
                Check(false, "справочник устарел: первое расхождение на строке " + (line + 1) + ".\n  в файле:    " + (line < a.Length ? a[line] : "(конец)") +
                             "\n  из записей: " + (line < e.Length ? e[line] : "(конец)") +
                             "\n  Обновите: GLASS_UPDATE_DOCS=1 dotnet run -c Release -- документация");
            }
            else Check(true, "справочник совпадает с записями");
        }

        // ------------------------------------------------------------------ страницы карточек

        private static void CardPagesExist()
        {
            foreach (string topic in LanguageReference.All.Select(e => e.DocTopic).Distinct())
            {
                string path = Path.Combine(DocsDir, topic.Replace('/', Path.DirectorySeparatorChar));
                Check(File.Exists(path), "карточки ссылаются на страницу docs/" + topic + ", и она существует");
            }

            // Страница не просто есть — она рассказывает про те элементы, которые на неё ссылаются.
            foreach (ReferenceEntry entry in LanguageReference.All)
            {
                string path = Path.Combine(DocsDir, entry.DocTopic.Replace('/', Path.DirectorySeparatorChar));
                if (!File.Exists(path)) continue;
                bool mentioned = Regex.IsMatch(ReadText(path), @"(?<![\p{L}\p{N}_])" + Regex.Escape(entry.Name) + @"(?![\p{L}\p{N}_])");
                Check(mentioned, "docs/" + entry.DocTopic + " упоминает «" + entry.Name + "» (" + entry.KindText + ")");
            }
        }

        // ------------------------------------------------------------------ ссылки

        private static void LinksAreAlive()
        {
            var linkedFromIndex = new HashSet<string>(StringComparer.Ordinal);
            string indexPath = Path.Combine(DocsDir, "README.md");
            Check(File.Exists(indexPath), "docs/README.md (оглавление) существует");

            foreach (string page in Pages())
            {
                string name = Rel(page);
                foreach (Match link in Regex.Matches(ReadText(page), @"\[[^\]]*\]\((?<target>[^)\s]+)\)"))
                {
                    string target = link.Groups["target"].Value;
                    if (Regex.IsMatch(target, @"^(https?:|mailto:|#)")) continue;
                    string file = target.Split('#')[0];
                    if (file.Length == 0) continue;
                    string resolved = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(page)!, file.Replace('/', Path.DirectorySeparatorChar)));
                    Check(File.Exists(resolved) || Directory.Exists(resolved), name + ": ссылка «" + target + "» ведёт на существующий файл");
                    if (page == indexPath) linkedFromIndex.Add(Path.GetRelativePath(DocsDir, resolved).Replace('\\', '/'));
                }
            }

            foreach (string page in Pages())
            {
                string name = Rel(page);
                if (name == "README.md") continue;
                Check(linkedFromIndex.Contains(name), "оглавление docs/README.md ссылается на " + name);
            }
        }
    }
}
