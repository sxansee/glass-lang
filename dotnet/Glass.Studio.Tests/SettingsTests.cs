using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// Настройки пользователя и то, что на них держится: недавние файлы, размер шрифта редактора, открытие по пути
    /// (меню «Недавние файлы», перетаскивание файла на окно).
    /// </summary>
    public static class SettingsTests
    {
        public static void Register()
        {
            Add("настройки: значения приводятся к допустимым", NormalizeKeepsValuesSane);
            Add("настройки: файл — запись и чтение, повреждённый и отсутствующий", StoreRoundTrip);
            Add("настройки: размер шрифта редактора — пределы, сброс, запоминание", EditorFontSize);
            Add("настройки: недавние файлы — порядок, повтор, предел, исчезнувший файл", RecentFiles);
            Add("настройки: открытие по пути — подтверждение потери, выполнение, ошибка", OpenPath);
            Add("настройки: файл настроек лежит в каталоге данных приложения", SettingsFileLocation);
        }

        /// <summary>Хранилище в памяти: счётчики вместо настоящего файла.</summary>
        private sealed class MemoryStore : ISettingsStore
        {
            public UserSettings Stored = new UserSettings();
            public int Saves;

            public UserSettings Load() => Stored;

            public bool Save(UserSettings settings)
            {
                Saves++;
                // копия: чтобы тест видел именно сохранённое состояние, а не живой объект ViewModel
                Stored = new UserSettings { RecentFiles = settings.RecentFiles.ToList(), EditorFontSize = settings.EditorFontSize };
                return true;
            }
        }

        private sealed class Env : IDisposable
        {
            public readonly string Dir = Path.Combine(Path.GetTempPath(), "glass-studio-settings-" + Guid.NewGuid().ToString("N"));
            public Env() => Directory.CreateDirectory(Dir);
            public string Make(string name, string text = "fn main() { print(1) }\n")
            {
                string path = Path.Combine(Dir, name);
                File.WriteAllText(path, text);
                return path;
            }
            public void Dispose() { try { Directory.Delete(Dir, true); } catch (IOException) { } }
        }

        private static void NormalizeKeepsValuesSane()
        {
            var settings = new UserSettings
            {
                RecentFiles = new List<string> { "a.glass", "", "  ", "A.GLASS", "b.glass", "c", "d", "e", "f", "g", "h", "i" },
                EditorFontSize = 500,
            }.Normalize();
            CheckEqual(UserSettings.MaxRecentFiles, settings.RecentFiles.Count, "список обрезан до предела");
            CheckEqual("a.glass,b.glass,c", string.Join(",", settings.RecentFiles.Take(3)), "пустые и повторы (без учёта регистра) убраны, порядок сохранён");
            CheckEqual(UserSettings.MaxEditorFontSize, settings.EditorFontSize, "слишком крупный шрифт ограничен сверху");

            CheckEqual(UserSettings.MinEditorFontSize, UserSettings.ClampFontSize(0), "нулевой шрифт ограничен снизу");
            CheckEqual(UserSettings.DefaultEditorFontSize, UserSettings.ClampFontSize(double.NaN), "NaN — шрифт по умолчанию");
            CheckEqual(UserSettings.DefaultEditorFontSize, UserSettings.ClampFontSize(double.PositiveInfinity), "бесконечность — шрифт по умолчанию");
            CheckEqual(20.0, UserSettings.ClampFontSize(20), "допустимое значение не меняется");
            CheckEqual(0, new UserSettings { RecentFiles = null! }.Normalize().RecentFiles.Count, "null вместо списка — пустой список");
        }

        private static void StoreRoundTrip()
        {
            using var env = new Env();
            string path = Path.Combine(env.Dir, "nested", "settings.json");   // каталога ещё нет — хранилище создаёт его само
            var store = new UserSettingsStore(path);

            CheckEqual(UserSettings.DefaultEditorFontSize, store.Load().EditorFontSize, "файла нет — настройки по умолчанию");
            Check(store.Save(new UserSettings { RecentFiles = new List<string> { "x.glass", "y.glass" }, EditorFontSize = 22 }), "сохранено");
            Check(!File.Exists(path + ".tmp"), "временный файл убран");
            UserSettings loaded = store.Load();
            CheckEqual("x.glass,y.glass", string.Join(",", loaded.RecentFiles), "недавние файлы вернулись");
            CheckEqual(22.0, loaded.EditorFontSize, "размер шрифта вернулся");

            File.WriteAllText(path, "{ это не json");
            UserSettings bad = store.Load();
            CheckEqual(UserSettings.DefaultEditorFontSize, bad.EditorFontSize, "повреждённый файл — настройки по умолчанию");
            Check(File.Exists(path + ".bad") && !File.Exists(path), "испорченный файл отложен в .bad, а не перезаписан молча");

            File.WriteAllText(path, "{\"EditorFontSize\": 9999, \"RecentFiles\": [\"z\", \"z\"]}");
            UserSettings clamped = store.Load();
            CheckEqual(UserSettings.MaxEditorFontSize, clamped.EditorFontSize, "значения из файла приводятся к допустимым");
            CheckEqual(1, clamped.RecentFiles.Count, "повторы из файла убраны");

            // Сохранить некуда: на месте каталога лежит файл — это ошибка записи, а не падение.
            string blocker = env.Make("blocker");
            Check(!new UserSettingsStore(Path.Combine(blocker, "settings.json")).Save(new UserSettings()), "запись невозможна — false, без исключения");
        }

        private static void EditorFontSize()
        {
            var store = new MemoryStore { Stored = new UserSettings { EditorFontSize = 20 } };
            var vm = new MainViewModel(new FakeEditor(), new FakeDialogs(), liveCheckDelayMs: -1, settings: store);
            CheckEqual(20.0, vm.EditorFontSize, "размер шрифта прочитан из настроек");

            var changed = new List<string>();
            vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName ?? "");
            vm.EditorFontLargerCommand.Execute(null);
            CheckEqual(21.0, vm.EditorFontSize, "крупнее на пункт");
            Check(changed.Contains("EditorFontSize"), "ViewModel сообщает об изменении размера");
            CheckEqual(21.0, store.Stored.EditorFontSize, "новый размер сохранён");
            vm.EditorFontSmallerCommand.Execute(null);
            vm.EditorFontSmallerCommand.Execute(null);
            CheckEqual(19.0, vm.EditorFontSize, "мельче");

            int saves = store.Saves;
            vm.EditorFontSize = 19;   // то же значение — ни события, ни записи
            CheckEqual(saves, store.Saves, "без изменения размера файл не переписывается");

            vm.EditorFontSize = 1000;
            CheckEqual(UserSettings.MaxEditorFontSize, vm.EditorFontSize, "предел сверху");
            vm.EditorFontSize = -5;
            CheckEqual(UserSettings.MinEditorFontSize, vm.EditorFontSize, "предел снизу");
            vm.EditorFontResetCommand.Execute(null);
            CheckEqual(UserSettings.DefaultEditorFontSize, vm.EditorFontSize, "сброс — размер по умолчанию");

            // Без хранилища (тесты, первый запуск без прав на запись) всё работает в памяти.
            var inMemory = new MainViewModel(new FakeEditor(), new FakeDialogs(), liveCheckDelayMs: -1);
            inMemory.EditorFontLargerCommand.Execute(null);
            CheckEqual(UserSettings.DefaultEditorFontSize + 1, inMemory.EditorFontSize, "без хранилища размер меняется в памяти");
        }

        private static void RecentFiles()
        {
            using var env = new Env();
            var store = new MemoryStore();
            var editor = new FakeEditor();
            var dialogs = new FakeDialogs();
            var vm = new MainViewModel(editor, dialogs, liveCheckDelayMs: -1, settings: store);
            Check(!vm.HasRecentFiles && vm.RecentFiles.Count == 0, "в начале список пуст — пункт меню недоступен");

            string a = env.Make("a.glass", "// a\nfn main() { }\n");
            string b = env.Make("b.glass", "// b\nfn main() { }\n");
            Check(vm.OpenPath(a), "открыт a");
            Check(vm.OpenPath(b), "открыт b");
            CheckEqual("b.glass,a.glass", string.Join(",", vm.RecentFiles.Select(r => r.Title)), "самый свежий первым");
            Check(vm.HasRecentFiles, "пункт меню доступен");
            CheckEqual(b, vm.RecentFiles[0].Path, "полный путь — для подсказки");

            // Повторное открытие поднимает файл наверх, а не дублирует.
            Check(vm.OpenPath(a), "a снова");
            CheckEqual("a.glass,b.glass", string.Join(",", vm.RecentFiles.Select(r => r.Title)), "повтор поднят наверх");
            CheckEqual(2, store.Stored.RecentFiles.Count, "в настройках тоже без повторов");

            // Через пункт меню.
            vm.RecentFiles[1].OpenCommand.Execute(null);
            CheckEqual("// b\nfn main() { }\n", editor.Text, "пункт меню открывает файл");
            CheckEqual(b, vm.CurrentFilePath, "путь текущего файла обновился");

            // «Сохранить как…» тоже делает файл недавним.
            string saved = Path.Combine(env.Dir, "saved.glass");
            dialogs.SavePath = saved;
            editor.Text = "fn main() { print(5) }\n";
            vm.NotifyTextChanged();
            Check(vm.SaveAs(), "сохранено под новым именем");
            CheckEqual("saved.glass", vm.RecentFiles[0].Title, "сохранённый файл — первый в недавних");

            // Предел и порядок при большом числе файлов.
            for (int i = 0; i < 12; i++) vm.OpenPath(env.Make("f" + i + ".glass"));
            CheckEqual(UserSettings.MaxRecentFiles, vm.RecentFiles.Count, "список не длиннее предела");
            CheckEqual("f11.glass", vm.RecentFiles[0].Title, "последний открытый — первый");

            // Файл исчез: статус объясняет, а из списка он убирается.
            string gone = env.Make("gone.glass");
            Check(vm.OpenPath(gone), "файл ещё есть");
            File.Delete(gone);
            Check(!vm.OpenPath(gone), "файл удалён — открыть нельзя");
            CheckContains(vm.StatusText, "Не удалось открыть файл", "статус говорит, что случилось");
            Check(vm.RecentFiles.All(r => r.Title != "gone.glass"), "исчезнувший файл убран из недавних");

            // Список переживает перезапуск: новая ViewModel читает те же настройки.
            var next = new MainViewModel(new FakeEditor(), new FakeDialogs(), liveCheckDelayMs: -1, settings: store);
            CheckEqual(vm.RecentFiles.Count, next.RecentFiles.Count, "после перезапуска список на месте");
            CheckEqual(vm.RecentFiles[0].Path, next.RecentFiles[0].Path, "и в том же порядке");
        }

        private static async Task OpenPath()
        {
            using var env = new Env();
            string first = env.Make("one.glass", "fn main() { print(1) }\n");
            string second = env.Make("two.glass", "fn main() { print(2) }\n");
            var editor = new FakeEditor { Text = "fn main() { print(0) }\n" };
            var dialogs = new FakeDialogs();
            var vm = new MainViewModel(editor, dialogs, liveCheckDelayMs: -1);

            // Несохранённые изменения: отказались терять — файл не открыт, текст на месте.
            vm.NotifyTextChanged();
            dialogs.Confirm = false;
            Check(!vm.OpenPath(first), "отказ терять изменения — файл не открыт");
            CheckEqual("fn main() { print(0) }\n", editor.Text, "текст не тронут");
            CheckEqual(1, dialogs.ConfirmCalls, "спросили один раз");

            // Согласились — открыт.
            dialogs.Confirm = true;
            Check(vm.OpenPath(first), "согласились — файл открыт");
            CheckEqual("fn main() { print(1) }\n", editor.Text, "текст файла в редакторе");
            Check(!vm.IsModified, "после открытия — не изменён");

            // Без изменений спрашивать нечего.
            int calls = dialogs.ConfirmCalls;
            Check(vm.OpenPath(second), "второй файл");
            CheckEqual(calls, dialogs.ConfirmCalls, "без правок вопроса нет");

            // Во время выполнения файл не открывается — как и обычное «Открыть».
            editor.Text = "fn main() { while true { } }\n";
            Task run = vm.StartOrContinueAsync();
            await Task.Delay(100);
            if (vm.State == IdeState.Running)
            {
                Check(!vm.OpenPath(first), "идёт выполнение — файл не открывается");
                Check(vm.RecentFiles.Count == 0 || vm.RecentFiles.All(r => !r.OpenCommand.CanExecute(null)), "пункты «Недавних» недоступны, пока идёт выполнение");
                vm.Stop();
            }
            await run;
        }

        private static void SettingsFileLocation()
        {
            CheckEqual(Path.Combine(AppPaths.DataDirectory, "settings.json"), AppPaths.SettingsFile, "настройки — settings.json в каталоге данных");
        }
    }
}
