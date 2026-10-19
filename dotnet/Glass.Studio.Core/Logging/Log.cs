// Логгер Glass Studio: журнал работы IDE — запуски, паузы, ошибки, файловые операции, необработанные исключения.
//
// Зачем он студенту и разработчику: когда «что-то не так» (программа не запустилась, окно упало, схема кучи
// пуста), в файле журнала остаётся ответ — что делала среда и чем закончилось. Файл лежит в
// %LOCALAPPDATA%\GlassStudio\logs, по одному на день (glass-studio-ГГГГ-ММ-ДД.log), старше 14 дней — удаляются.
//
// Устройство: AppLog пишет в файл и держит последние N записей в памяти (для вкладки «Журнал»). Потокобезопасно
// (команды отладчика идут на пуле потоков). Сбой записи в файл (диск заполнен, нет прав) НЕ роняет IDE: файловый
// журнал выключается, память продолжает работать, причина доступна в FileError.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace Glass.Studio.Logging
{
    public enum LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 }

    public sealed record LogEntry(DateTime Time, LogLevel Level, string Source, string Message, string? Exception)
    {
        public string LevelText => Level switch
        {
            LogLevel.Debug => "DEBUG",
            LogLevel.Info => "INFO",
            LogLevel.Warning => "WARN",
            _ => "ERROR",
        };

        public string TimeText => Time.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture);

        /// <summary>Строка файла: «2026-10-07 12:34:56.789 [INFO ] Источник: сообщение» (+ исключение с отступом).</summary>
        public string Format()
        {
            var text = new StringBuilder();
            text.Append(Time.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture));
            text.Append(" [").Append(LevelText.PadRight(5)).Append("] ").Append(Source).Append(": ").Append(Message);
            if (Exception != null)
            {
                foreach (string line in Exception.Split('\n'))
                {
                    text.Append(Environment.NewLine).Append("    ").Append(line.TrimEnd('\r'));
                }
            }
            return text.ToString();
        }
    }

    public interface ILog
    {
        void Write(LogLevel level, string source, string message, Exception? exception = null);
    }

    public static class LogExtensions
    {
        public static void Debug(this ILog log, string source, string message) => log.Write(LogLevel.Debug, source, message);
        public static void Info(this ILog log, string source, string message) => log.Write(LogLevel.Info, source, message);
        public static void Warning(this ILog log, string source, string message, Exception? exception = null) =>
            log.Write(LogLevel.Warning, source, message, exception);
        public static void Error(this ILog log, string source, string message, Exception? exception = null) =>
            log.Write(LogLevel.Error, source, message, exception);
    }

    /// <summary>Журнал, который ничего не делает — значение по умолчанию там, где логгер не передан.</summary>
    public sealed class NullLog : ILog
    {
        public static readonly NullLog Instance = new NullLog();

        public void Write(LogLevel level, string source, string message, Exception? exception = null)
        {
        }
    }

    public sealed class AppLog : ILog
    {
        private const string FilePrefix = "glass-studio-";
        private const string FileExtension = ".log";

        private readonly object _gate = new object();
        private readonly string _directory;
        private readonly int _keepDays;
        private readonly int _capacity;
        private readonly Func<DateTime> _clock;
        private readonly List<LogEntry> _memory = new List<LogEntry>();
        private string? _currentPath;
        private bool _fileFailed;
        private string? _fileError;

        /// <param name="directory">Каталог файлов журнала (создаётся при первой записи).</param>
        /// <param name="keepDays">Файлы старше стольких дней удаляются при смене даты.</param>
        /// <param name="memoryCapacity">Сколько последних записей помнить для вкладки «Журнал».</param>
        /// <param name="clock">Источник времени (подменяется в тестах); по умолчанию — местное время.</param>
        public AppLog(string directory, int keepDays = 14, int memoryCapacity = 1000, Func<DateTime>? clock = null)
        {
            _directory = directory ?? throw new ArgumentNullException(nameof(directory));
            _keepDays = Math.Max(1, keepDays);
            _capacity = Math.Max(10, memoryCapacity);
            _clock = clock ?? (() => DateTime.Now);
        }

        public string Directory => _directory;

        /// <summary>Файл журнала за сегодня (даже если ещё ничего не записано).</summary>
        public string CurrentFilePath => PathFor(_clock());

        /// <summary>Не null, если запись в файл отключена из-за ошибки (текст ошибки).</summary>
        public string? FileError
        {
            get { lock (_gate) return _fileError; }
        }

        /// <summary>Запись добавлена (вызывается вне блокировки, из потока, который писал).</summary>
        public event Action<LogEntry>? EntryWritten;

        public void Write(LogLevel level, string source, string message, Exception? exception = null)
        {
            LogEntry entry = new LogEntry(_clock(), level, source, message, exception?.ToString());
            lock (_gate)
            {
                _memory.Add(entry);
                if (_memory.Count > _capacity) _memory.RemoveRange(0, _memory.Count - _capacity);
                AppendToFile(entry);
            }
            EntryWritten?.Invoke(entry);
        }

        /// <summary>Копия последних записей (старые — первыми).</summary>
        public IReadOnlyList<LogEntry> Snapshot()
        {
            lock (_gate) return _memory.ToList();
        }

        private string PathFor(DateTime time) =>
            Path.Combine(_directory, FilePrefix + time.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture) + FileExtension);

        private void AppendToFile(LogEntry entry)
        {
            if (_fileFailed) return;
            try
            {
                string path = PathFor(entry.Time);
                if (path != _currentPath)
                {
                    System.IO.Directory.CreateDirectory(_directory);
                    DeleteOldFiles(entry.Time);
                    _currentPath = path;
                }
                File.AppendAllText(path, entry.Format() + Environment.NewLine, new UTF8Encoding(false));
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is NotSupportedException)
            {
                // Журнал не должен ронять IDE: отключаем файл, остаёмся с памятью.
                _fileFailed = true;
                _fileError = ex.Message;
            }
        }

        private void DeleteOldFiles(DateTime now)
        {
            DateTime border = now.Date.AddDays(-_keepDays);
            foreach (string file in System.IO.Directory.EnumerateFiles(_directory, FilePrefix + "*" + FileExtension))
            {
                string name = Path.GetFileNameWithoutExtension(file).Substring(FilePrefix.Length);
                if (DateTime.TryParseExact(name, "yyyy-MM-dd", CultureInfo.InvariantCulture, DateTimeStyles.None, out DateTime day) && day < border)
                {
                    try { File.Delete(file); } catch (IOException) { } catch (UnauthorizedAccessException) { }
                }
            }
        }
    }
}
