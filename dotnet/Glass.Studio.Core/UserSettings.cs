using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Glass.Studio.Logging;

namespace Glass.Studio
{
    /// <summary>
    /// Что приложение помнит между запусками: недавние файлы и размер шрифта редактора. Прогресс курса хранится отдельно
    /// (<see cref="Course.CourseProgressStore"/>) — у него своя жизнь и свой файл.
    /// </summary>
    public sealed class UserSettings
    {
        public const int MaxRecentFiles = 8;
        public const double DefaultEditorFontSize = 14;
        public const double MinEditorFontSize = 8;
        public const double MaxEditorFontSize = 48;

        /// <summary>Недавние файлы, самый свежий первым.</summary>
        public List<string> RecentFiles { get; set; } = new List<string>();

        /// <summary>Размер шрифта редактора в пунктах (показ на проекторе — крупнее).</summary>
        public double EditorFontSize { get; set; } = DefaultEditorFontSize;

        /// <summary>
        /// Приводит значения из файла в допустимый вид: файл правят руками и портят обрывом записи, а окно не должно из-за
        /// этого получить шрифт в 0 пунктов или список из тысячи строк.
        /// </summary>
        public UserSettings Normalize()
        {
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            RecentFiles = (RecentFiles ?? new List<string>())
                .Where(p => !string.IsNullOrWhiteSpace(p) && seen.Add(p))
                .Take(MaxRecentFiles)
                .ToList();
            EditorFontSize = ClampFontSize(EditorFontSize);
            return this;
        }

        public static double ClampFontSize(double size)
        {
            if (double.IsNaN(size) || double.IsInfinity(size)) return DefaultEditorFontSize;
            return Math.Max(MinEditorFontSize, Math.Min(MaxEditorFontSize, size));
        }
    }

    /// <summary>Где живут настройки. Интерфейс нужен ViewModel и тестам: настоящий файл им не требуется.</summary>
    public interface ISettingsStore
    {
        UserSettings Load();

        /// <summary>false — не удалось сохранить (причина в журнале); работа продолжается с настройками в памяти.</summary>
        bool Save(UserSettings settings);
    }

    /// <summary>Настройки в JSON-файле: чтение терпимо к порче, запись — целиком или никак.</summary>
    public sealed class UserSettingsStore : ISettingsStore
    {
        private readonly string _path;
        private readonly ILog _log;

        public UserSettingsStore(string path, ILog? log = null)
        {
            _path = path ?? throw new ArgumentNullException(nameof(path));
            _log = log ?? NullLog.Instance;
        }

        public string Path => _path;

        public UserSettings Load()
        {
            if (!File.Exists(_path)) return new UserSettings();
            try
            {
                UserSettings? settings = JsonSerializer.Deserialize<UserSettings>(File.ReadAllText(_path));
                return (settings ?? new UserSettings()).Normalize();
            }
            catch (Exception ex) when (ex is JsonException || ex is IOException || ex is UnauthorizedAccessException || ex is NotSupportedException)
            {
                _log.Warning("Настройки", "Файл настроек повреждён — начинаем с настроек по умолчанию: " + _path, ex);
                try { File.Move(_path, _path + ".bad", overwrite: true); } catch (Exception) { /* не получилось — не страшно */ }
                return new UserSettings();
            }
        }

        public bool Save(UserSettings settings)
        {
            try
            {
                string? directory = System.IO.Path.GetDirectoryName(_path);
                if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                string temp = _path + ".tmp";
                File.WriteAllText(temp, JsonSerializer.Serialize(settings.Normalize(), new JsonSerializerOptions { WriteIndented = true }));
                File.Move(temp, _path, overwrite: true);
                return true;
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is NotSupportedException)
            {
                _log.Warning("Настройки", "Не удалось сохранить настройки: " + _path, ex);
                return false;
            }
        }
    }
}
