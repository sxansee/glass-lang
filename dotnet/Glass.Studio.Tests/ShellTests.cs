using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Windows.Input;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// «Склейка» WPF-оболочки с ядром — то, что можно проверить без Windows: возможности ядра действительно доступны
    /// из окон (меню, кнопки, вкладки), обработчики существуют, данные лежат там, где обещано.
    /// Появились потому, что курс и журнал были готовы и протестированы в ядре, но пользователь их не видел: окна не были
    /// подключены, а ни один тест этого не замечал.
    /// </summary>
    public static class ShellTests
    {
        public static void Register()
        {
            Add("оболочка: пути данных приложения (%LOCALAPPDATA%\\GlassStudio)", AppPathsAreStable);
            Add("оболочка: курс доступен из главного окна (меню, кнопка, окно, обработчики)", CourseIsReachable);
            Add("оболочка: окно курса показывает все возможности view-model курса", CourseWindowCoversViewModel);
            Add("оболочка: вкладка «Журнал», пункты «Справки» и обработчики необработанных исключений подключены", LogAndHelpAreWired);
            Add("оболочка: каждая команда главной ViewModel выведена в окно", MainWindowCoversViewModel);
            Add("оболочка: удобство редактирования — «Правка», поиск, шрифт, недавние файлы, перетаскивание", EditingComfortIsWired);
            Add("оболочка: манифест — Per-Monitor V2 DPI и обычные права", DpiAwareness);
        }

        private static void MainWindowCoversViewModel()
        {
            string xaml = Read("MainWindow.xaml");
            string code = Read("MainWindow.xaml.cs");
            Check(xaml.Length > 0 && code.Length > 0, "MainWindow.xaml и MainWindow.xaml.cs найдены");

            // Возможность без кнопки — это возможность, которой нет: так уже было с курсом и журналом.
            // Исключения — две команды, которые окно вызывает другим путём; он проверяется отдельно, ниже.
            var viaOtherPath = new[] { nameof(MainViewModel.OpenExampleCommand), nameof(MainViewModel.SelectHeapNodeCommand) };
            Check(xaml.Contains("ItemsSource=\"{Binding ExampleMenu}\""), "примеры открываются пунктами меню (ExampleMenuItem.OpenCommand)");
            string heapView = File.ReadAllText(Path.Combine(WpfDir() ?? "", "Views", "HeapGraphView.xaml.cs"));
            Check(heapView.Contains("ViewModel?.SelectHeapNode("), "узлы схемы кучи выбираются прямым вызовом SelectHeapNode из обработчиков");
            foreach (PropertyInfo command in typeof(MainViewModel).GetProperties()
                         .Where(p => typeof(ICommand).IsAssignableFrom(p.PropertyType) && !viaOtherPath.Contains(p.Name)))
            {
                bool shown = Regex.IsMatch(xaml, @"\{Binding\s+" + Regex.Escape(command.Name) + @"[\s,}]") || code.Contains(command.Name, StringComparison.Ordinal);
                Check(shown, "MainWindow показывает команду " + command.Name);
            }
        }

        private static void EditingComfortIsWired()
        {
            string xaml = Read("MainWindow.xaml");
            string code = Read("MainWindow.xaml.cs");
            string host = Read("Editor/EditorHost.cs");

            // «Правка»: стандартные команды, адресованные именно текстовой области редактора (иначе они недоступны, пока
            // фокус не в тексте), — и меню «Правка» существует.
            Check(xaml.Contains("Header=\"_Правка\""), "в меню есть «Правка»");
            foreach (string command in new[] { "Undo", "Redo", "Cut", "Copy", "Paste", "SelectAll", "Find" })
            {
                Check(Regex.IsMatch(xaml, "Command=\"" + command + "\"[^>]*CommandTarget=\"\\{Binding ElementName=CodeEditor, Path=TextArea\\}\""),
                      "«Правка»: команда " + command + " адресована текстовой области редактора");
            }
            Check(host.Contains("SearchPanel.Install"), "панель поиска установлена в редактор (Ctrl+F)");
            Check(File.Exists(Path.Combine(WpfDir() ?? "", "Editor", "RussianSearchLocalization.cs")), "подписи панели поиска переведены");

            Check(Regex.IsMatch(xaml, "ItemsSource=\"\\{Binding RecentFiles\\}\""), "«Недавние файлы» привязаны к списку RecentFiles");
            Check(Regex.IsMatch(xaml, "FontSize=\"\\{Binding EditorFontSize"), "размер шрифта редактора берётся из настроек, а не из разметки");
            Check(xaml.Contains("PreviewMouseWheel=\"OnEditorMouseWheel\"") && code.Contains("Keyboard.Modifiers != ModifierKeys.Control"),
                  "Ctrl+колесо над редактором меняет размер шрифта");
            Check(xaml.Contains("AllowDrop=\"True\"") && xaml.Contains("PreviewDrop=\"OnPreviewDrop\"") && code.Contains("DataFormats.FileDrop") &&
                  code.Contains("_viewModel.OpenPath("), "файл, брошенный на окно, открывается через OpenPath (с вопросом о потере изменений)");
            Check(code.Contains("new UserSettingsStore(AppPaths.SettingsFile"), "настройки хранятся в файле каталога данных приложения");
        }

        private static void DpiAwareness()
        {
            string csproj = Read("Glass.Studio.csproj");
            string manifest = Read("app.manifest");
            Check(csproj.Contains("<ApplicationManifest>app.manifest</ApplicationManifest>"), "проект подключает app.manifest");
            Check(manifest.Contains("PerMonitorV2"), "манифест объявляет Per-Monitor V2: окно не мутнеет при переносе на проектор с другим масштабом");
            Check(manifest.Contains("asInvoker"), "запуск без повышения прав");
            Check(System.Xml.Linq.XDocument.Parse(manifest).Root != null, "манифест — корректный XML");
        }

        private static string? WpfDir()
        {
            for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                string candidate = Path.Combine(dir.FullName, "Glass.Studio", "Glass.Studio.csproj");
                if (File.Exists(candidate)) return Path.GetDirectoryName(candidate);
            }
            return null;
        }

        private static string Read(string relative)
        {
            string? dir = WpfDir();
            if (dir == null) return "";
            string path = Path.Combine(dir, relative.Replace('/', Path.DirectorySeparatorChar));
            return File.Exists(path) ? File.ReadAllText(path) : "";
        }

        private static string RepoRead(string relative)
        {
            string? dir = WpfDir();
            if (dir == null) return "";
            string path = Path.GetFullPath(Path.Combine(dir, "..", "..", relative.Replace('/', Path.DirectorySeparatorChar)));
            return File.Exists(path) ? File.ReadAllText(path) : "";
        }

        private static void AppPathsAreStable()
        {
            CheckEqual("GlassStudio", Path.GetFileName(AppPaths.DataDirectory), "каталог данных называется GlassStudio");
            CheckEqual(Path.Combine(AppPaths.DataDirectory, "logs"), AppPaths.LogDirectory, "журналы — в подкаталоге logs");
            CheckEqual(Path.Combine(AppPaths.DataDirectory, "course-progress.json"), AppPaths.CourseProgressFile, "прогресс курса — course-progress.json");
            Check(Path.IsPathRooted(AppPaths.DataDirectory), "путь абсолютный");
            Check(RepoRead("README.md").Contains(@"%LOCALAPPDATA%\GlassStudio"), "README объясняет, где лежат данные приложения");
        }

        private static void CourseIsReachable()
        {
            if (WpfDir() == null) { Console.WriteLine("  (пропущено: каталог Glass.Studio не найден рядом с тестами)"); return; }

            string xaml = Read("MainWindow.xaml");
            string code = Read("MainWindow.xaml.cs");
            Check(Regex.Matches(xaml, @"Click=""OnOpenCourse""").Count >= 2, "MainWindow.xaml: вход в курс есть и в меню, и на панели инструментов");
            Check(Regex.IsMatch(xaml, @"Header=""_Курс по Glass"""), "в меню пункт «Курс по Glass»");
            Check(xaml.IndexOf("_Курс по Glass", StringComparison.Ordinal) < xaml.IndexOf("_Справка", StringComparison.Ordinal),
                  "пункт «Курс по Glass» стоит рядом со «Справкой» (перед ней)");
            Check(Regex.IsMatch(code, @"void\s+OnOpenCourse\s*\(\s*object\s+\w+\s*,\s*RoutedEventArgs\s+\w+\s*\)"), "MainWindow.xaml.cs: обработчик OnOpenCourse");
            Check(code.Contains("new CourseWindow(", StringComparison.Ordinal), "обработчик создаёт CourseWindow");
            Check(code.Contains("AppServices.CreateCourse(", StringComparison.Ordinal), "курс получает общий журнал и файл прогресса из AppServices");

            string services = Read("Services/AppServices.cs");
            Check(services.Contains("CourseProgressStore(AppPaths.CourseProgressFile", StringComparison.Ordinal), "AppServices: прогресс курса — в %LOCALAPPDATA%\\GlassStudio");

            Check(Read("CourseWindow.xaml").Length > 0 && Read("CourseWindow.xaml.cs").Length > 0, "окно курса существует (CourseWindow.xaml + .cs)");
            Check(Regex.IsMatch(Read("CourseWindow.xaml"), @"d:DataContext=""\{d:DesignInstance Type=vm:CourseViewModel\}"""), "окно курса привязано к CourseViewModel");
            Check(Read("Behaviors/RichTextBehavior.cs").Contains("RegisterAttached", StringComparison.Ordinal), "есть RichTextBehavior для жирного и кода в тексте уроков");
        }

        private static void CourseWindowCoversViewModel()
        {
            if (WpfDir() == null) { Console.WriteLine("  (пропущено: каталог Glass.Studio не найден рядом с тестами)"); return; }

            string xaml = Read("CourseWindow.xaml");
            string code = Read("CourseWindow.xaml.cs");

            // Каждая команда view-model курса обязана быть где-то в окне — иначе возможность есть, а кнопки нет.
            foreach (PropertyInfo command in typeof(CourseViewModel).GetProperties().Where(p => typeof(ICommand).IsAssignableFrom(p.PropertyType) && p.Name != "SelectLessonCommand"))   // выбор урока — у строки списка (LessonItem.SelectCommand)
            {
                bool shown = Regex.IsMatch(xaml, @"\{Binding\s+" + Regex.Escape(command.Name) + @"[\s,}]") || code.Contains(command.Name, StringComparison.Ordinal);
                Check(shown, "CourseWindow показывает команду " + command.Name);
            }

            // То, что ученик должен видеть. Если свойство переименуют или забудут показать — тест скажет.
            string[] shownProperties =
            {
                "Chapters", "ProgressText", "ProgressPercent", "LevelText", "StarsSummary", "StartText", "IsAllDone",
                "ShowWelcome", "HasLesson", "LessonTitle", "LessonSubtitle", "Theory", "TaskText",
                "IsHintVisible", "HintText", "IsSolutionVisible", "SolutionText",
                "HasResult", "IsSuccess", "IsFailure", "ResultTitle", "ResultMessage",
                "ShowOutputComparison", "ActualOutput", "ExpectedOutput", "StatusText",
            };
            foreach (string name in shownProperties)
            {
                Check(typeof(CourseViewModel).GetProperty(name) != null, "у CourseViewModel есть свойство " + name);
                Check(Regex.IsMatch(xaml, @"\{Binding\s+" + Regex.Escape(name) + @"[\s,}]"), "CourseWindow показывает " + name);
            }
        }

        private static void LogAndHelpAreWired()
        {
            if (WpfDir() == null) { Console.WriteLine("  (пропущено: каталог Glass.Studio не найден рядом с тестами)"); return; }

            string xaml = Read("MainWindow.xaml");
            string code = Read("MainWindow.xaml.cs");
            Check(Regex.IsMatch(xaml, @"<TabItem\s+Header=""Журнал"""), "вкладка «Журнал» в нижней панели");
            Check(xaml.Contains(@"DataContext=""{Binding LogPanel}""", StringComparison.Ordinal), "вкладка привязана к LogPanel");
            Check(xaml.Contains(@"x:Name=""LogList""", StringComparison.Ordinal), "список записей журнала (LogList)");
            foreach (string handler in new[] { "OnOpenLogFolder", "OnOpenProjectPage", "OnAbout" })
            {
                Check(xaml.Contains(@"Click=""" + handler + @"""", StringComparison.Ordinal), "MainWindow.xaml: пункт «Справки» " + handler);
                Check(Regex.IsMatch(code, @"void\s+" + handler + @"\s*\("), "MainWindow.xaml.cs: обработчик " + handler);
            }
            Check(Regex.IsMatch(code, @"[Ll]ogPanel\s*=\s*new\s+LogPanelViewModel\(\s*AppServices\.Log"), "MainWindow создаёт LogPanelViewModel на общем журнале");
            Check(code.Contains("log: AppServices.Log", StringComparison.Ordinal), "MainViewModel пишет в общий журнал");

            string app = Read("App.xaml.cs");
            Check(app.Contains("DispatcherUnhandledException +=", StringComparison.Ordinal), "App: необработанные исключения интерфейса");
            Check(app.Contains("AppDomain.CurrentDomain.UnhandledException +=", StringComparison.Ordinal), "App: необработанные исключения процесса");
            Check(app.Contains("TaskScheduler.UnobservedTaskException +=", StringComparison.Ordinal), "App: необработанные исключения фоновых задач");
        }
    }
}
