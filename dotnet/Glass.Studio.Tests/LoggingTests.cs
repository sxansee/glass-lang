using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Glass.Studio.Logging;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    public static class LoggingTests
    {
        public static void Register()
        {
            Add("журнал: запись в файл, формат, кириллица, исключения", FileFormat);
            Add("журнал: кольцевая память хранит последние записи по порядку", MemoryRing);
            Add("журнал: новый день — новый файл, старые файлы удаляются", Rotation);
            Add("журнал: 8 потоков пишут одновременно — ничего не теряется", Concurrency);
            Add("журнал: сбой записи в файл не роняет IDE", FailureResilience);
            Add("журнал: событие EntryWritten вызывается вне блокировки", EventOutsideLock);
            Add("журнал: панель — фильтр уровня, очистка, контекст UI", Panel);
        }

        private static string TempDir() => Path.Combine(Path.GetTempPath(), "glass-log-tests-" + Guid.NewGuid().ToString("N"));

        private static void FileFormat()
        {
            string dir = TempDir();
            try
            {
                DateTime now = new DateTime(2026, 10, 7, 12, 34, 56, 789);
                var log = new AppLog(dir, clock: () => now);
                log.Info("Запуск", "Программа запущена: привет, мир");
                log.Error("Ядро", "Сбой", new InvalidOperationException("плохо"));
                string path = log.CurrentFilePath;
                CheckEqual("glass-studio-2026-10-07.log", Path.GetFileName(path), "имя файла по дате");
                string[] lines = File.ReadAllLines(path);
                CheckEqual("2026-10-07 12:34:56.789 [INFO ] Запуск: Программа запущена: привет, мир", lines[0], "формат строки");
                CheckContains(lines[1], "[ERROR] Ядро: Сбой", "уровень ERROR выровнен");
                Check(lines.Any(l => l.StartsWith("    ", StringComparison.Ordinal) && l.Contains("InvalidOperationException", StringComparison.Ordinal)),
                    "исключение — следующими строками с отступом");
                Check(lines.Any(l => l.Contains("плохо", StringComparison.Ordinal)), "текст исключения записан");
                // UTF-8 без BOM: первые байты — цифры года, а не EF BB BF.
                byte[] bytes = File.ReadAllBytes(path);
                CheckEqual((byte)'2', bytes[0], "файл без BOM");
                CheckEqual(true, log.FileError == null, "ошибок записи нет");
            }
            finally { if (Directory.Exists(dir)) Directory.Delete(dir, true); }
        }

        private static void MemoryRing()
        {
            var log = new AppLog(TempDir() + "-unused", memoryCapacity: 10);   // каталог не создаётся — файловый журнал здесь не нужен
            for (int i = 1; i <= 25; i++) log.Info("T", "запись " + i);
            IReadOnlyList<LogEntry> snapshot = log.Snapshot();
            CheckEqual(10, snapshot.Count, "память ограничена");
            CheckEqual("запись 16", snapshot[0].Message, "самая старая из оставшихся");
            CheckEqual("запись 25", snapshot[9].Message, "самая новая — последняя");
            // Снимок — копия: новые записи его не меняют.
            log.Info("T", "ещё");
            CheckEqual(10, snapshot.Count, "снимок не меняется");
            var dir = new DirectoryInfo(log.Directory);
            if (dir.Exists) dir.Delete(true);
        }

        private static void Rotation()
        {
            string dir = TempDir();
            try
            {
                Directory.CreateDirectory(dir);
                File.WriteAllText(Path.Combine(dir, "glass-studio-2026-09-01.log"), "очень старый");
                File.WriteAllText(Path.Combine(dir, "glass-studio-2026-09-25.log"), "недавний");
                File.WriteAllText(Path.Combine(dir, "чужой-файл.txt"), "не трогать");

                DateTime now = new DateTime(2026, 10, 1, 9, 0, 0);
                var log = new AppLog(dir, keepDays: 14, clock: () => now);
                log.Info("A", "первый день");
                CheckEqual(false, File.Exists(Path.Combine(dir, "glass-studio-2026-09-01.log")), "файл старше 14 дней удалён");
                CheckEqual(true, File.Exists(Path.Combine(dir, "glass-studio-2026-09-25.log")), "недавний сохранён");
                CheckEqual(true, File.Exists(Path.Combine(dir, "чужой-файл.txt")), "посторонние файлы не трогаем");

                now = new DateTime(2026, 10, 2, 0, 0, 1);
                log.Info("A", "второй день");
                CheckEqual(true, File.Exists(Path.Combine(dir, "glass-studio-2026-10-01.log")), "файл первого дня остался");
                CheckEqual(true, File.Exists(Path.Combine(dir, "glass-studio-2026-10-02.log")), "новый день — новый файл");
                CheckEqual(1, File.ReadAllLines(Path.Combine(dir, "glass-studio-2026-10-02.log")).Length, "в новом файле одна запись");

                now = new DateTime(2026, 10, 20, 8, 0, 0);
                log.Info("A", "через 18 дней");
                CheckEqual(false, File.Exists(Path.Combine(dir, "glass-studio-2026-09-25.log")), "и 25 сентября ушёл за границу 14 дней");
                CheckEqual(false, File.Exists(Path.Combine(dir, "glass-studio-2026-10-01.log")), "и 1 октября тоже");
                CheckEqual(true, File.Exists(Path.Combine(dir, "glass-studio-2026-10-20.log")), "сегодняшний есть");
            }
            finally { if (Directory.Exists(dir)) Directory.Delete(dir, true); }
        }

        private static async Task Concurrency()
        {
            string dir = TempDir();
            try
            {
                var log = new AppLog(dir, memoryCapacity: 1000);
                var tasks = Enumerable.Range(0, 8).Select(t => Task.Run(() =>
                {
                    for (int i = 0; i < 100; i++) log.Info("поток" + t, "сообщение " + i);
                })).ToArray();
                await Task.WhenAll(tasks);
                CheckEqual(800, File.ReadAllLines(log.CurrentFilePath).Length, "все 800 строк в файле");
                CheckEqual(800, log.Snapshot().Count, "и все 800 в памяти");
                Check(File.ReadAllLines(log.CurrentFilePath).All(l => l.Contains("] поток", StringComparison.Ordinal)),
                    "строки не перемешаны посреди записи");
            }
            finally { if (Directory.Exists(dir)) Directory.Delete(dir, true); }
        }

        private static void FailureResilience()
        {
            // «Каталог журналов» на самом деле — файл: CreateDirectory падает с IOException.
            string blocker = Path.Combine(Path.GetTempPath(), "glass-log-blocker-" + Guid.NewGuid().ToString("N"));
            File.WriteAllText(blocker, "я файл");
            try
            {
                var log = new AppLog(Path.Combine(blocker, "logs"));
                log.Info("T", "первая");
                log.Error("T", "вторая", new Exception("x"));
                CheckEqual(2, log.Snapshot().Count, "память работает");
                Check(log.FileError != null, "причина отключения файла доступна: " + log.FileError);
                log.Info("T", "третья");
                CheckEqual(3, log.Snapshot().Count, "после сбоя — по-прежнему не падает");
            }
            finally { File.Delete(blocker); }
        }

        private static void EventOutsideLock()
        {
            var log = new AppLog(TempDir() + "-unused");
            int seen = -1;
            log.EntryWritten += _ => seen = log.Snapshot().Count;   // если бы событие шло под блокировкой в другом потоке — взаимоблокировка
            var thread = new Thread(() => log.Info("T", "x"));
            thread.Start();
            Check(thread.Join(TimeSpan.FromSeconds(5)), "поток не завис");
            CheckEqual(1, seen, "обработчик увидел свою запись");
            var dir = new DirectoryInfo(log.Directory);
            if (dir.Exists) dir.Delete(true);
            NullLog.Instance.Info("T", "ничего не происходит");
            NullLog.Instance.Error("T", "и тут", new Exception("x"));
        }

        private sealed class PostedContext : SynchronizationContext
        {
            public readonly List<(SendOrPostCallback Callback, object? State)> Posted = new List<(SendOrPostCallback, object?)>();
            public override void Post(SendOrPostCallback d, object? state) => Posted.Add((d, state));
            public void Pump()
            {
                foreach ((SendOrPostCallback callback, object? state) in Posted.ToList()) callback(state);
                Posted.Clear();
            }
        }

        private static void Panel()
        {
            string dir = TempDir() + "-unused";
            var log = new AppLog(dir);
            log.Debug("T", "отладка");
            log.Info("T", "инфо");
            var panel = new LogPanelViewModel(log, uiContext: null);
            CheckEqual(1, panel.Entries.Count, "по умолчанию (Info и выше) Debug скрыт, старые записи подхвачены");
            log.Warning("T", "предупреждение");
            log.Error("T", "ошибка", new Exception("e"));
            log.Debug("T", "ещё отладка");
            CheckEqual(3, panel.Entries.Count, "новые записи появляются сами");
            Check(panel.Entries.Last().IsError && !panel.Entries[1].IsError && panel.Entries[1].IsWarning, "флаги уровня для раскраски");
            CheckContains(panel.Entries.Last().Details, "System.Exception", "в подсказке — исключение");

            panel.MinLevelIndex = 0;
            CheckEqual(5, panel.Entries.Count, "Debug включён — видны все");
            panel.MinLevelIndex = 3;
            CheckEqual(1, panel.Entries.Count, "только ошибки");
            panel.MinLevelIndex = 99;
            CheckEqual(3, panel.MinLevelIndex, "уровень ограничен диапазоном");
            panel.MinLevelIndex = 1;
            CheckEqual("Записей: 3", panel.Summary, "сводка");

            panel.Clear();
            CheckEqual(0, panel.Entries.Count, "очистка");
            log.Info("T", "после очистки");
            CheckEqual(1, panel.Entries.Count, "после очистки новые идут");

            string? opened = null;
            panel.OpenFolder = path => opened = path;
            panel.OpenFolderCommand.Execute(null);
            CheckEqual(dir, opened, "кнопка «Открыть папку» передаёт каталог журналов");
            Check(panel.FilePath.EndsWith(".log", StringComparison.Ordinal), "путь к файлу текущего дня");

            // Контекст UI: запись из другого потока попадает в коллекцию только через Post.
            var context = new PostedContext();
            var contextual = new LogPanelViewModel(log, context);
            int before = contextual.Entries.Count;
            log.Info("T", "из другого потока");
            CheckEqual(before, contextual.Entries.Count, "до обработки очереди UI коллекция не меняется");
            CheckEqual(1, context.Posted.Count, "запись поставлена в очередь UI");
            context.Pump();
            CheckEqual(before + 1, contextual.Entries.Count, "после обработки очереди — на месте");

            var d = new DirectoryInfo(dir);
            if (d.Exists) d.Delete(true);
        }
    }
}
