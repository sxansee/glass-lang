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
