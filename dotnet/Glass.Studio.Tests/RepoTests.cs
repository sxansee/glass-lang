using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// Гигиена репозитория: версия, адреса и служебные файлы согласованы между собой. Такие вещи ломаются
    /// тихо (забыли поднять номер в одном месте, остался чужой адрес, скрипт перестал запускать набор тестов),
    /// а замечаются уже при релизе.
    /// </summary>
    public static class RepoTests
    {
        public static void Register()
        {
            Add("репозиторий: версия едина (Directory.Build.props, CMakeLists.txt, AppInfo, CHANGELOG)", VersionsAgree);
            Add("репозиторий: нет адресов-заглушек, ссылки ведут на свой репозиторий", RepositoryUrlConsistent);
            Add("репозиторий: служебные файлы на месте (лицензия, CI, релиз, шаблоны)", HousekeepingFiles);
            Add("репозиторий: workflow ссылаются на существующие скрипты и проекты", WorkflowReferences);
            Add("репозиторий: test-all.* запускают все C++-наборы из tests/CMakeLists.txt", ScriptsCoverAllSuites);
            Add("репозиторий: тесты, линкуемые с glass_capi, получают DLL рядом с exe (на Windows нет RPATH)", DllNextToTests);
        }

        private static string? FindRoot()
        {
            for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                if (File.Exists(Path.Combine(dir.FullName, "CMakeLists.txt")) && Directory.Exists(Path.Combine(dir.FullName, "dotnet")))
                    return dir.FullName;
            }
            return null;
        }

        private static string Full(string relative)
        {
            string root = FindRoot() ?? throw new InvalidOperationException("не найден корень репозитория");
            return Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        }

        private static bool Exists(string relative) => File.Exists(Full(relative)) || Directory.Exists(Full(relative));

        /// <summary>Текст файла репозитория; нет файла — пустая строка (проверки сами скажут, чего не хватает).</summary>
        private static string Read(string relative) => File.Exists(Full(relative)) ? File.ReadAllText(Full(relative)) : "";

        // ------------------------------------------------------------------ версия

        private static void VersionsAgree()
        {
            Match props = Regex.Match(Read("dotnet/Directory.Build.props"), @"<Version>\s*([^<\s]+)\s*</Version>");
            Check(props.Success, "Directory.Build.props: есть <Version>");
            if (!props.Success) return;
            string version = props.Groups[1].Value;
            Check(Regex.IsMatch(version, @"^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$"), "версия — семантическая (X.Y.Z[-суффикс]): " + version);

            Match cmake = Regex.Match(Read("CMakeLists.txt"), @"project\(\s*Glass\s+VERSION\s+(\d+\.\d+\.\d+)");
            Check(cmake.Success, "CMakeLists.txt: project(Glass VERSION X.Y.Z ...)");
            if (cmake.Success)
                CheckEqual(version.Split('-')[0], cmake.Groups[1].Value, "версия в CMakeLists.txt (числовая часть) совпадает с Directory.Build.props");

            CheckEqual(version, AppInfo.Version, "AppInfo.Version (из сборки) совпадает с Directory.Build.props");
            Check(Read("CHANGELOG.md").Contains("## [" + version + "]"), "CHANGELOG.md: есть раздел «## [" + version + "]»");
        }

        // ------------------------------------------------------------------ адреса

        private static readonly string[] TextExtensions = { ".md", ".yml", ".yaml", ".cs", ".props", ".csproj", ".xaml", ".ps1", ".sh", ".txt", ".cpp", ".h" };
        private static readonly string[] SkippedDirs = { "build", "bin", "obj", "out", ".git", ".vs" };

        private static IEnumerable<string> TextFiles(string root)
        {
            foreach (string file in Directory.EnumerateFiles(root))
                if (TextExtensions.Contains(Path.GetExtension(file))) yield return file;
            foreach (string dir in Directory.EnumerateDirectories(root))
            {
                if (SkippedDirs.Contains(Path.GetFileName(dir))) continue;
                foreach (string file in TextFiles(dir)) yield return file;
            }
        }

        private static void RepositoryUrlConsistent()
        {
            string placeholder = "your" + "-name";   // склеено, чтобы сам тест не содержал заглушку
            Match url = Regex.Match(AppInfo.RepositoryUrl, @"^https://github\.com/([^/\s]+/[^/\s]+)$");
            Check(url.Success, "AppInfo.RepositoryUrl — адрес вида https://github.com/<владелец>/<репозиторий>: " + AppInfo.RepositoryUrl);
            Check(!AppInfo.RepositoryUrl.Contains(placeholder), "AppInfo.RepositoryUrl — не заглушка");

            Check(Read("README.md").Contains(AppInfo.RepositoryUrl), "README.md (бейджи, ссылки) ведёт на " + AppInfo.RepositoryUrl);
            Check(Read("dotnet/Directory.Build.props").Contains(AppInfo.RepositoryUrl), "Directory.Build.props: RepositoryUrl совпадает с AppInfo");

            string root = FindRoot()!;
            var withPlaceholder = TextFiles(root).Where(f => File.ReadAllText(f).Contains(placeholder)).Select(f => Path.GetRelativePath(root, f)).ToList();
            CheckEqual("", string.Join(", ", withPlaceholder), "нигде в репозитории не осталось адреса-заглушки");
        }

        // ------------------------------------------------------------------ служебные файлы

        private static void HousekeepingFiles()
        {
            string[] required =
            {
                "LICENSE", ".gitattributes", "CONTRIBUTING.md", "CHANGELOG.md", "RELEASING.md", "THIRD-PARTY-NOTICES.md",
                ".github/workflows/ci.yml", ".github/workflows/release.yml", ".github/PULL_REQUEST_TEMPLATE.md",
                ".github/ISSUE_TEMPLATE/bug_report.md", ".github/ISSUE_TEMPLATE/feature_request.md", "dotnet/Directory.Build.props",
            };
            foreach (string file in required) Check(Exists(file), "есть " + file);
            Check(Read("LICENSE").Contains("MIT License"), "LICENSE — MIT");
        }

        private static void WorkflowReferences()
        {
            foreach (string workflow in new[] { ".github/workflows/ci.yml", ".github/workflows/release.yml" })
            {
                string text = Read(workflow);
                Check(text.Length > 0, workflow + ": файл не пуст");
                var paths = Regex.Matches(text, @"(?:\./)?((?:scripts|dotnet)/[\w./-]+\.(?:sh|ps1|sln|csproj|props))")
                                 .Select(m => m.Groups[1].Value).Distinct().ToList();
                Check(paths.Count > 0, workflow + ": найдены ссылки на файлы репозитория");
                foreach (string path in paths) Check(Exists(path), workflow + " ссылается на существующий " + path);
            }
        }

        // ------------------------------------------------------------------ DLL рядом с тестами

        private static void DllNextToTests()
        {
            string cmake = Read("tests/CMakeLists.txt");
            var targets = Regex.Matches(cmake, @"target_link_libraries\(\s*(\w+)\s+PRIVATE[^)]*\bglass_capi\b")
                               .Select(m => m.Groups[1].Value).Distinct().ToList();
            Check(targets.Count > 0, "tests/CMakeLists.txt: найден хотя бы один тест, использующий glass_capi");
            foreach (string target in targets)
            {
                bool copied = Regex.IsMatch(cmake, @"add_custom_command\(\s*TARGET\s+" + Regex.Escape(target) + @"\s+POST_BUILD[^)]*glass_capi");
                Check(copied, target + ": после сборки glass_capi.dll копируется рядом с exe (иначе на Windows тест не запускается: код 0xC0000135, вывода нет)");
            }
        }

        // ------------------------------------------------------------------ скрипты тестов

        private static void ScriptsCoverAllSuites()
        {
            var suites = Regex.Matches(Read("tests/CMakeLists.txt"), @"add_test\(NAME\s+(\w+?)_tests\b")
                              .Select(m => m.Groups[1].Value).ToList();
            Check(suites.Count >= 11, "tests/CMakeLists.txt: найдено наборов: " + suites.Count);

            Match sh = Regex.Match(Read("scripts/test-all.sh"), @"for\s+t\s+in\s+([^;]+);");
            Check(sh.Success, "test-all.sh: найден цикл по наборам");
            var inSh = sh.Success ? sh.Groups[1].Value.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries).ToList() : new List<string>();
            CheckEqual("", string.Join(", ", suites.Except(inSh)), "test-all.sh запускает все наборы (не хватает)");
            CheckEqual("", string.Join(", ", inSh.Except(suites)), "test-all.sh не запускает несуществующих наборов (лишние)");

            Match ps = Regex.Match(Read("scripts/test-all.ps1"), @"foreach\s*\(\s*\$t\s+in\s+([^)]+)\)");
            Check(ps.Success, "test-all.ps1: найден цикл по наборам");
            var inPs = ps.Success ? Regex.Matches(ps.Groups[1].Value, @"'(\w+)'").Select(m => m.Groups[1].Value).ToList() : new List<string>();
            CheckEqual("", string.Join(", ", suites.Except(inPs)), "test-all.ps1 запускает все наборы (не хватает)");
            CheckEqual("", string.Join(", ", inPs.Except(suites)), "test-all.ps1 не запускает несуществующих наборов (лишние)");
        }
    }
}
