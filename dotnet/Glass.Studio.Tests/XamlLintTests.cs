using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;
using System.Xml.Schema;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// Проверки WPF-оболочки, которые возможны без Windows: XAML-линтер на настоящих файлах и на
    /// заведомо битых примерах, схема подсветки синтаксиса, соответствие подсветки лексеру, csproj.
    /// </summary>
    public static class XamlLintTests
    {
        private const string Header =
            "xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" " +
            "xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\" " +
            "xmlns:d=\"http://schemas.microsoft.com/expression/blend/2008\" " +
            "xmlns:vm=\"clr-namespace:Glass.Studio.ViewModels;assembly=Glass.Studio.Core\" " +
            "xmlns:heap=\"clr-namespace:Glass.Studio.Heap;assembly=Glass.Studio.Core\"";

        public static void Register()
        {
            Add("WPF: XAML проекта — линтер без замечаний", LintRealFiles);
            Add("WPF: линтер ловит заведомо битый XAML (самопроверка)", LintCatchesBrokenXaml);
            Add("WPF: линтер не ругается на корректный XAML (самопроверка)", LintAcceptsGoodXaml);
            Add("WPF: линтер ловит ошибки, внесённые в НАСТОЯЩИЕ файлы (мутации)", LintCatchesMutations);
            Add("WPF: Glass.xshd валиден по схеме AvalonEdit, ключевые слова = лексер", HighlightingMatchesLexer);
            Add("WPF: csproj — ресурс подсветки, пакет редактора, копирование DLL", ProjectFile);
        }

        // ------------------------------------------------------------------ поиск файлов

        private static string? FindUp(string relative)
        {
            for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                string candidate = Path.Combine(dir.FullName, relative);
                if (File.Exists(candidate) || Directory.Exists(candidate)) return candidate;
            }
            return null;
        }

        private static XamlLinter CreateLinter(string wpfDir)
        {
            // Ключи словарей уровня приложения: App.xaml → <ResourceDictionary Source="..."/>.
            var appKeys = new List<string>();
            XDocument app = XDocument.Load(Path.Combine(wpfDir, "App.xaml"));
            foreach (XElement merged in app.Descendants().Where(e => e.Name.LocalName == "ResourceDictionary" && e.Attribute("Source") != null))
            {
                string path = Path.Combine(wpfDir, ((string)merged.Attribute("Source")!).Replace('/', Path.DirectorySeparatorChar));
                appKeys.AddRange(XamlLinter.KeysOf(XDocument.Load(path).Root!));
            }

            string allSources = string.Join("\n", Directory.EnumerateFiles(wpfDir, "*.cs", SearchOption.AllDirectories)
                .Where(f => !f.Contains(Path.DirectorySeparatorChar + "obj" + Path.DirectorySeparatorChar))
                .Select(File.ReadAllText));
            return new XamlLinter(typeof(MainViewModel).Assembly,
                name => Regex.IsMatch(allSources, @"\b(class|struct|enum|interface)\s+" + Regex.Escape(name) + @"\b"),
                appKeys);
        }

        // ------------------------------------------------------------------ настоящие файлы

        private static void LintRealFiles()
        {
            string? csproj = FindUp(Path.Combine("Glass.Studio", "Glass.Studio.csproj"));
            if (csproj == null)
            {
                Console.WriteLine("  (пропущено: каталог Glass.Studio не найден рядом с тестами)");
                return;
            }
            string wpfDir = Path.GetDirectoryName(csproj)!;
            XamlLinter linter = CreateLinter(wpfDir);

            List<string> files = Directory.EnumerateFiles(wpfDir, "*.xaml", SearchOption.AllDirectories)
                .Where(f => !f.Contains(Path.DirectorySeparatorChar + "obj" + Path.DirectorySeparatorChar))
                .OrderBy(f => f, StringComparer.Ordinal).ToList();
            Check(files.Count >= 5, "найдено XAML-файлов: " + files.Count);

            int errors = 0;
            foreach (string file in files)
            {
                string relative = Path.GetRelativePath(wpfDir, file);
                XDocument document;
                try
                {
                    document = XDocument.Load(file, LoadOptions.SetLineInfo);
                }
                catch (XmlException ex)
                {
                    Check(false, relative + ": XML невалиден — " + ex.Message);
                    errors++;
                    continue;
                }

                string codeBehindPath = file + ".cs";
                string? codeBehind = File.Exists(codeBehindPath) ? File.ReadAllText(codeBehindPath) : null;
                foreach (string error in linter.Lint(relative, document, codeBehind))
                {
                    Check(false, error);
                    errors++;
                }
            }
            CheckEqual(0, errors, "замечаний линтера к XAML проекта");

            // Линтер обязан реально что-то разбирать, а не молча пропускать всё.
            Check(linter.BindingsChecked >= 150, "проверено привязок: " + linter.BindingsChecked);
            Check(linter.ResourceRefsChecked >= 60, "проверено ссылок на ресурсы: " + linter.ResourceRefsChecked);
            Check(linter.HandlersChecked >= 6, "проверено обработчиков событий: " + linter.HandlersChecked);
            Check(linter.StaticsChecked >= 3, "проверено x:Static: " + linter.StaticsChecked);
            Console.WriteLine("  XAML: файлов " + files.Count + ", привязок " + linter.BindingsChecked + ", ссылок на ресурсы " +
                              linter.ResourceRefsChecked + ", обработчиков " + linter.HandlersChecked + ", x:Static " + linter.StaticsChecked);
        }

        // ------------------------------------------------------------------ самопроверка линтера

        private static XamlLinter ScratchLinter() =>
            new XamlLinter(typeof(MainViewModel).Assembly, name => name == "ExistingLocalType", new[] { "AppBrush" });

        private static IReadOnlyList<string> LintText(string xaml, string? codeBehind = "void OnGood(object sender, System.EventArgs e) { }")
        {
            XDocument document = XDocument.Parse(xaml, LoadOptions.SetLineInfo);
            return ScratchLinter().Lint("scratch.xaml", document, codeBehind);
        }

        private static void LintCatchesBrokenXaml()
        {
            string xaml =
                "<Window x:Class=\"T.W\" " + Header + " d:DataContext=\"{d:DesignInstance Type=vm:MainViewModel}\">\n" +
                "  <Window.Resources>\n" +
                "    <Style x:Key=\"First\" TargetType=\"TextBlock\" BasedOn=\"{StaticResource Later}\" />\n" +
                "    <Style x:Key=\"Later\" TargetType=\"TextBlock\" />\n" +
                "    <Style x:Key=\"Dup\" TargetType=\"TextBlock\" />\n" +
                "    <Style x:Key=\"Dup\" TargetType=\"TextBlock\" />\n" +
                "  </Window.Resources>\n" +
                "  <StackPanel>\n" +
                "    <TextBlock Text=\"{Binding NoSuchProperty}\" />\n" +                                  // 1: нет свойства
                "    <TextBox Text=\"{Binding StatusText}\" />\n" +                                         // 2: TwoWay по умолчанию на свойстве без set
                "    <TextBlock Text=\"{Binding Output, Mode=OneWay}\" />\n" +                               // ок
                "    <Button Style=\"{StaticResource NoSuchStyle}\" />\n" +                                  // 3: нет ресурса
                "    <Button Click=\"OnMissing\" />\n" +                                                      // 4: нет обработчика
                "    <Button Click=\"OnGood\" />\n" +                                                         // ок
                "    <TextBlock Style=\"{StaticResource AppBrush}\"><TextBlock.Style><Style TargetType=\"TextBlock\" /></TextBlock.Style></TextBlock>\n" + // 5: Style дважды
                "    <TextBlock Visibility=\"{Binding IsRunning}\" />\n" +                                   // 6: bool → Visibility
                "    <Button Command=\"{Binding Output}\" />\n" +                                            // 7: не ICommand
                "    <ListBox ItemsSource=\"{Binding CallStack}\">\n" +
                "      <ListBox.ItemTemplate><DataTemplate><TextBlock Text=\"{Binding Nope}\" /></DataTemplate></ListBox.ItemTemplate>\n" + // 8: контекст элемента
                "    </ListBox>\n" +
                "    <TextBlock><TextBlock.Style><Style TargetType=\"TextBlock\"><Style.Triggers>\n" +
                "      <DataTrigger Binding=\"{Binding IsRunning}\" Value=\"Maybe\"><Setter Property=\"Text\" Value=\"x\" /></DataTrigger>\n" + // 9: литерал
                "    </Style.Triggers></Style></TextBlock.Style></TextBlock>\n" +
                "    <TextBlock Text=\"{x:Static vm:MainViewModel.Nothing}\" />\n" +                         // 10: нет static-члена
                "    <TextBlock Text=\"{Binding Zoom}\" x:Name=\"A\" />\n" +
                "    <TextBlock x:Name=\"A\" />\n" +                                                          // 11: дубль x:Name
                "    <ItemsControl ItemsSource=\"{Binding Output}\" />\n" +                                  // 12: ItemsSource — не коллекция
                "    <TextBlock Tag=\"{x:Type vm:NoSuchType}\" />\n" +                                       // 13: нет типа
                "    <Button Command=\"{Binding StepIntoCommand}\" Click=\"OnBadSignature\" />\n" +          // 14: сигнатура обработчика
                "  </StackPanel>\n" +
                "</Window>";
            string codeBehind = "void OnGood(object sender, System.EventArgs e) { }\nvoid OnBadSignature(int x) { }";
            string joined = string.Join("\n", LintText(xaml, codeBehind));

            string[] expected =
            {
                "нет публичного свойства «NoSuchProperty»",
                "нет публичного set",
                "ресурс не найден",
                "«OnMissing»",
                "задано и атрибутом, и элементом",
                "Visibility без Converter",
                "это не ICommand",
                "нет публичного свойства «Nope»",
                "не подходит к типу Boolean",
                "нет публичного статического члена «Nothing»",
                "x:Name «A» объявлено дважды",
                "это не коллекция",
                "тип Glass.Studio.ViewModels.NoSuchType не найден",
                "должен иметь сигнатуру",
                "ресурс объявлен ниже",
                "объявлен несколько раз в одном словаре",
            };
            foreach (string fragment in expected) CheckContains(joined, fragment, "линтер обязан поймать: " + fragment);
        }

        private static void LintAcceptsGoodXaml()
        {
            string xaml =
                "<Window x:Class=\"T.W\" " + Header + " d:DataContext=\"{d:DesignInstance Type=vm:MainViewModel}\">\n" +
                "  <Window.Resources>\n" +
                "    <Style x:Key=\"Cell\" TargetType=\"TextBlock\">\n" +
                "      <Style.Triggers><DataTrigger Binding=\"{Binding IsCurrent}\" Value=\"True\"><Setter Property=\"FontWeight\" Value=\"Bold\" /></DataTrigger></Style.Triggers>\n" +
                "    </Style>\n" +
                "  </Window.Resources>\n" +
                "  <StackPanel>\n" +
                "    <TextBox Text=\"{Binding Output, Mode=OneWay}\" />\n" +
                "    <TextBox Text=\"{Binding SelectedBottomTab, Mode=TwoWay}\" />\n" +
                "    <TextBlock Text=\"{Binding Zoom, StringFormat={}{0:P0}}\" />\n" +
                "    <Button Command=\"{Binding StepIntoCommand}\" Click=\"OnGood\" />\n" +
                "    <TextBlock Visibility=\"{Binding StatusText, Converter={StaticResource AppBrush}}\" />\n" +
                "    <ListBox ItemsSource=\"{Binding CallStack}\" SelectedItem=\"{Binding SelectedFrame, Mode=TwoWay}\">\n" +
                "      <ListBox.ItemTemplate><DataTemplate DataType=\"{x:Type vm:StackFrameItem}\">\n" +
                "        <TextBlock Text=\"{Binding Function}\" Style=\"{StaticResource Cell}\" />\n" +
                "      </DataTemplate></ListBox.ItemTemplate>\n" +
                "    </ListBox>\n" +
                "    <Border Height=\"{x:Static heap:HeapMetrics.RowHeight}\" />\n" +
                "    <ContentControl DataContext=\"{Binding Inspector}\" Content=\"{Binding Title}\" />\n" +
                "    <ItemsControl ItemsSource=\"{Binding Inspector.Fields}\" />\n" +
                "    <TextBlock Text=\"{Binding ElementName=Other, Path=Anything}\" />\n" +
                "  </StackPanel>\n" +
                "</Window>";
            IReadOnlyList<string> errors = LintText(xaml);
            CheckEqual(0, errors.Count, "на корректном XAML замечаний нет: " + string.Join(" | ", errors));
        }

        // «Мутационная» проверка: в копию настоящего файла вносим одну типичную ошибку и требуем, чтобы
        // линтер её заметил. Так проверяется не игрушечный XAML, а реальная структура проекта —
        // стили-ресурсы, шаблоны элементов, столбцы GridView, контекст из ItemsSource.
        private static void LintCatchesMutations()
        {
            string? csproj = FindUp(Path.Combine("Glass.Studio", "Glass.Studio.csproj"));
            if (csproj == null)
            {
                Console.WriteLine("  (пропущено: каталог Glass.Studio не найден)");
                return;
            }
            string wpfDir = Path.GetDirectoryName(csproj)!;
            XamlLinter linter = CreateLinter(wpfDir);

            (string File, string Old, string New, string Expected)[] mutations =
            {
                ("Views/HeapGraphView.xaml", "{Binding IsFromGarbage}", "{Binding IsFromGarbge}", "нет публичного свойства «IsFromGarbge»"),
                ("Views/HeapGraphView.xaml", "StaticResource EdgePathStyle}", "StaticResource EdgePathStyl}", "ресурс не найден"),
                ("Views/HeapGraphView.xaml", "<DataTemplate DataType=\"{x:Type heap:HeapEdgeModel}\">", "<DataTemplate DataType=\"{x:Type heap:HeapNodeModel}\">", "DataTemplate.DataType = HeapNodeModel"),
                ("Views/HeapGraphView.xaml", "{Binding HeapGraph.Width}", "{Binding HeapGraph.Widht}", "нет публичного свойства «Widht»"),
                ("Views/HeapGraphView.xaml", "MouseLeftButtonDown=\"OnNodeMouseDown\"", "MouseLeftButtonDown=\"OnNodeMouseDownn\"", "не найден в code-behind"),
                ("Views/ObjectInspectorView.xaml", "{Binding IsInherited}", "{Binding IsInheritd}", "нет публичного свойства «IsInheritd»"),
                ("Views/ObjectInspectorView.xaml", "ItemsSource=\"{Binding Fields}\"", "ItemsSource=\"{Binding Fieldz}\"", "нет публичного свойства «Fieldz»"),
                ("Views/ObjectInspectorView.xaml", "{Binding VTable.Count}", "{Binding VTable.Lenght}", "нет публичного свойства «Lenght»"),
                ("MainWindow.xaml", "{Binding Output, Mode=OneWay}", "{Binding Output}", "нет публичного set"),
                ("MainWindow.xaml", "{Binding SelectedFrame, Mode=TwoWay}", "{Binding SelectedFramee, Mode=TwoWay}", "нет публичного свойства «SelectedFramee»"),
                ("MainWindow.xaml", "DisplayMemberBinding=\"{Binding KindText}\"", "DisplayMemberBinding=\"{Binding KindTxt}\"", "нет публичного свойства «KindTxt»"),
                ("MainWindow.xaml", "DisplayMemberBinding=\"{Binding TypeText}\"", "DisplayMemberBinding=\"{Binding TypeTxt}\"", "нет публичного свойства «TypeTxt»"),
                ("MainWindow.xaml", "Closing=\"OnClosing\"", "Closing=\"OnClosingX\"", "не найден в code-behind"),
                ("MainWindow.xaml", "Command=\"{Binding StepOverCommand}\" ToolTip", "Command=\"{Binding StepOverCommnd}\" ToolTip", "нет публичного свойства «StepOverCommnd»"),
                ("MainWindow.xaml", "<Setter Property=\"Command\" Value=\"{Binding Command}\" />", "<Setter Property=\"Command\" Value=\"{Binding Comand}\" />", "нет публичного свойства «Comand»"),
                ("MainWindow.xaml", "{Binding Inspector}", "{Binding Inspektor}", "нет публичного свойства «Inspektor»"),
                ("MainWindow.xaml", "StaticResource ToolIcon}", "StaticResource ToolIkon}", "ресурс не найден"),
                ("MainWindow.xaml", "Binding=\"{Binding IsPaused}\" Value=\"True\">\r\n                                            <Setter Property=\"Text\"", "Binding=\"{Binding IsPaused}\" Value=\"Yes\">\r\n                                            <Setter Property=\"Text\"", "не подходит к типу Boolean"),
            };

            foreach ((string file, string oldText, string newText, string expected) in mutations)
            {
                string path = Path.Combine(wpfDir, file.Replace('/', Path.DirectorySeparatorChar));
                string text = File.ReadAllText(path).Replace("\r\n", "\n");
                string oldNormalized = oldText.Replace("\r\n", "\n");
                string newNormalized = newText.Replace("\r\n", "\n");
                int at = text.IndexOf(oldNormalized, StringComparison.Ordinal);
                if (at < 0)
                {
                    Check(false, "мутация неприменима — фрагмент не найден в " + file + ": " + oldNormalized.Replace("\n", " "));
                    continue;
                }
                string mutated = text.Substring(0, at) + newNormalized + text.Substring(at + oldNormalized.Length);
                string codeBehindPath = path + ".cs";
                string? codeBehind = File.Exists(codeBehindPath) ? File.ReadAllText(codeBehindPath) : null;
                string joined = string.Join("\n", linter.Lint(file, XDocument.Parse(mutated, LoadOptions.SetLineInfo), codeBehind));
                CheckContains(joined, expected, file + ": линтер обязан поймать мутацию «" + newNormalized.Replace("\n", " ").Trim() + "»");
            }
        }

        // ------------------------------------------------------------------ подсветка

        private static void HighlightingMatchesLexer()
        {
            string? xshdPath = FindUp(Path.Combine("Glass.Studio", "Editor", "Glass.xshd"));
            string? xsdPath = FindUp(Path.Combine("Glass.Studio.Tests", "Schemas", "ModeV2.xsd"));
            if (xshdPath == null || xsdPath == null)
            {
                Console.WriteLine("  (пропущено: Glass.xshd или ModeV2.xsd не найдены)");
                return;
            }

            // 1. Валидность по схеме самого AvalonEdit.
            var schemas = new XmlSchemaSet();
            schemas.Add("http://icsharpcode.net/sharpdevelop/syntaxdefinition/2008", xsdPath);
            var problems = new List<string>();
            var settings = new XmlReaderSettings { ValidationType = ValidationType.Schema, Schemas = schemas };
            settings.ValidationEventHandler += (_, e) => problems.Add(e.Message);
            using (XmlReader reader = XmlReader.Create(xshdPath, settings))
            {
                while (reader.Read()) { }
            }
            CheckEqual(0, problems.Count, "Glass.xshd валиден по ModeV2.xsd: " + string.Join("; ", problems));

            // 2. Ключевые слова подсветки = таблица ключевых слов лексера (языка).
            string? lexerPath = FindUp(Path.Combine("src", "lexer", "Lexer.cpp"));
            if (lexerPath == null)
            {
                Console.WriteLine("  (сверка с Lexer.cpp пропущена: исходники C++ не найдены)");
                return;
            }
            string lexer = File.ReadAllText(lexerPath);
            int tableStart = lexer.IndexOf("std::unordered_map<std::string, TokenType> table", StringComparison.Ordinal);
            int tableEnd = tableStart < 0 ? -1 : lexer.IndexOf("};", tableStart, StringComparison.Ordinal);
            Check(tableStart >= 0 && tableEnd > tableStart, "в Lexer.cpp найдена таблица ключевых слов");
            if (tableStart < 0 || tableEnd < 0) return;
            var languageKeywords = Regex.Matches(lexer.Substring(tableStart, tableEnd - tableStart), "\\{\"(\\w+)\",\\s*TokenType::")
                .Select(m => m.Groups[1].Value).ToHashSet();
            Check(languageKeywords.Count >= 30, "ключевых слов в лексере: " + languageKeywords.Count);

            XDocument xshd = XDocument.Load(xshdPath);
            bool IsBuiltinGroup(XElement word) => (string?)word.Parent?.Attribute("color") == "Builtin";
            var highlighted = xshd.Descendants().Where(e => e.Name.LocalName == "Word" && !IsBuiltinGroup(e)).Select(e => e.Value.Trim()).ToHashSet();
            var highlightedBuiltins = xshd.Descendants().Where(e => e.Name.LocalName == "Word" && IsBuiltinGroup(e)).Select(e => e.Value.Trim()).ToHashSet();
            // Слова интроспекции подсвечиваются правилом «~слово», а не как обычные ключевые слова.
            string introspectionRule = xshd.Descendants().First(e => e.Name.LocalName == "Rule" && (string?)e.Attribute("color") == "Introspection").Value;
            var introspection = Regex.Match(introspectionRule, @"\(([^)]+)\)").Groups[1].Value.Split('|').ToHashSet();

            foreach (string keyword in languageKeywords)
            {
                Check(highlighted.Contains(keyword) || introspection.Contains(keyword), "ключевое слово «" + keyword + "» не подсвечивается");
            }
            foreach (string word in highlighted)
            {
                Check(languageKeywords.Contains(word), "подсвечивается «" + word + "», которого нет в языке");
            }
            CheckEqual("address,gc,heap,layout,vtable", string.Join(",", introspection.OrderBy(x => x, StringComparer.Ordinal)), "слова интроспекции");

            // Встроенные функции подсвечиваются ровно те, что объявлены в Builtins.h (+ print, PI, E, которых там нет).
            string? builtinsPath = FindUp(Path.Combine("src", "bytecode", "Builtins.h"));
            if (builtinsPath != null)
            {
                string builtins = File.ReadAllText(builtinsPath);
                int from = builtins.IndexOf("findBuiltinFunction", StringComparison.Ordinal);
                int to = builtins.IndexOf("};", from, StringComparison.Ordinal);
                var functions = Regex.Matches(builtins.Substring(from, to - from), "\\{\"(\\w+)\",\\s*Builtin::").Select(m => m.Groups[1].Value)
                    .Where(n => n != "int" && n != "double" && n != "bool" && n != "string").ToHashSet();   // эти — типы, подсвечены как типы
                functions.UnionWith(new[] { "print", "PI", "E" });
                CheckEqual(string.Join(",", functions.OrderBy(x => x, StringComparer.Ordinal)),
                           string.Join(",", highlightedBuiltins.OrderBy(x => x, StringComparer.Ordinal)), "подсвеченные встроенные = Builtins.h + print, PI, E");
            }
        }

        // ------------------------------------------------------------------ проект

        private static void ProjectFile()
        {
            string? csprojPath = FindUp(Path.Combine("Glass.Studio", "Glass.Studio.csproj"));
            if (csprojPath == null)
            {
                Console.WriteLine("  (пропущено: Glass.Studio.csproj не найден)");
                return;
            }
            string wpfDir = Path.GetDirectoryName(csprojPath)!;
            XDocument csproj = XDocument.Load(csprojPath);

            string Prop(string name) => csproj.Descendants().FirstOrDefault(e => e.Name.LocalName == name)?.Value ?? "";
            CheckEqual("net8.0-windows", Prop("TargetFramework"), "целевая платформа WPF");
            CheckEqual("true", Prop("UseWPF"), "UseWPF");
            CheckEqual("WinExe", Prop("OutputType"), "оконное приложение");

            XElement? package = csproj.Descendants().FirstOrDefault(e => e.Name.LocalName == "PackageReference" && (string?)e.Attribute("Include") == "AvalonEdit");
            Check(package != null && ((string?)package.Attribute("Version") ?? "").StartsWith("6.3", StringComparison.Ordinal), "пакет AvalonEdit ветки 6.3");
            Check(csproj.Descendants().Any(e => e.Name.LocalName == "ProjectReference" && ((string?)e.Attribute("Include") ?? "").Contains("Glass.Studio.Core", StringComparison.Ordinal)),
                "ссылка на Glass.Studio.Core");

            // Встроенный ресурс подсветки: файл существует, а LogicalName совпадает с тем, что просит код.
            XElement? resource = csproj.Descendants().FirstOrDefault(e => e.Name.LocalName == "EmbeddedResource");
            Check(resource != null, "xshd встроен как ресурс");
            if (resource != null)
            {
                string include = ((string)resource.Attribute("Include")!).Replace('\\', Path.DirectorySeparatorChar);
                Check(File.Exists(Path.Combine(wpfDir, include)), "файл ресурса существует: " + include);
                string logicalName = (string?)resource.Attribute("LogicalName") ?? "";
                string loader = File.ReadAllText(Path.Combine(wpfDir, "Editor", "GlassHighlighting.cs"));
                CheckContains(loader, "\"" + logicalName + "\"", "код загрузки подсветки просит ресурс «" + logicalName + "»");
            }

            string text = File.ReadAllText(csprojPath);
            CheckContains(text, "glass_capi.dll", "проект копирует нативную библиотеку");
            CheckContains(text, "GlassNativeDll", "путь к библиотеке переопределяется из командной строки");
        }
    }
}
