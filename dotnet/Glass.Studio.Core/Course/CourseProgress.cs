// Прогресс ученика: какие уроки пройдены, сколько звёзд, подсказки. Хранится в JSON рядом с настройками
// (%LOCALAPPDATA%\GlassStudio\course-progress.json). Повреждённый файл не ломает курс: начинаем с чистого листа,
// а испорченный файл откладывается рядом (.bad) — вдруг пригодится.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Glass.Studio.Logging;

namespace Glass.Studio.Course
{
    public sealed class LessonRecord
    {
        public bool Completed { get; set; }
        public int Stars { get; set; }
        public int Attempts { get; set; }
        public bool HintUsed { get; set; }
        public bool SolutionShown { get; set; }
        public DateTime CompletedUtc { get; set; }
    }

    public sealed class CourseProgress
    {
        public Dictionary<string, LessonRecord> Lessons { get; set; } = new Dictionary<string, LessonRecord>();
        public string? LastLessonId { get; set; }

        public LessonRecord Record(string lessonId)
        {
            if (!Lessons.TryGetValue(lessonId, out LessonRecord? record)) Lessons[lessonId] = record = new LessonRecord();
            return record;
        }

        public bool IsCompleted(string lessonId) => Lessons.TryGetValue(lessonId, out LessonRecord? r) && r.Completed;
        public int CompletedCount => Lessons.Values.Count(r => r.Completed);
        public int TotalStars => Lessons.Values.Where(r => r.Completed).Sum(r => r.Stars);
    }

    public static class CourseScoring
    {
        public const int PointsPerStar = 10;

        /// <summary>3★ — с первого-второго раза без подсказок; 2★ — с подсказкой или после нескольких попыток; 1★ — после просмотра решения.</summary>
        public static int Stars(int attempts, bool hintUsed, bool solutionShown)
        {
            if (solutionShown) return 1;
            if (hintUsed || attempts > 2) return 2;
            return 3;
        }

        public static string StarsText(int stars) => new string('★', Math.Max(0, Math.Min(3, stars))) + new string('☆', 3 - Math.Max(0, Math.Min(3, stars)));

        private static readonly (double Fraction, string Title)[] Levels =
        {
            (0.00, "Новичок"),
            (0.12, "Ученик"),
            (0.33, "Практик"),
            (0.58, "Знаток"),
            (0.83, "Мастер"),
            (1.00, "Архитектор Glass"),
        };

        public static string LevelTitle(int completed, int total)
        {
            if (total <= 0) return Levels[0].Title;
            double fraction = (double)completed / total;
            string title = Levels[0].Title;
            foreach ((double border, string name) in Levels)
            {
                if (fraction >= border - 1e-9) title = name;
            }
            return title;
        }
    }

    public sealed class CourseProgressStore
    {
        private readonly string _path;
        private readonly ILog _log;

        public CourseProgressStore(string path, ILog? log = null)
        {
            _path = path ?? throw new ArgumentNullException(nameof(path));
            _log = log ?? NullLog.Instance;
        }

        public string Path => _path;

        public CourseProgress Load()
        {
            if (!File.Exists(_path)) return new CourseProgress();
            try
            {
                CourseProgress? progress = JsonSerializer.Deserialize<CourseProgress>(File.ReadAllText(_path));
                return progress ?? new CourseProgress();
            }
            catch (Exception ex) when (ex is JsonException || ex is IOException || ex is UnauthorizedAccessException || ex is NotSupportedException)
            {
                _log.Warning("Курс", "Файл прогресса повреждён — начинаем заново: " + _path, ex);
                try { File.Move(_path, _path + ".bad", overwrite: true); } catch (Exception) { /* не получилось — не страшно */ }
                return new CourseProgress();
            }
        }

        /// <summary>false — не удалось сохранить (причина в журнале); курс при этом продолжает работать.</summary>
        public bool Save(CourseProgress progress)
        {
            try
            {
                string? directory = System.IO.Path.GetDirectoryName(_path);
                if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                string temp = _path + ".tmp";
                File.WriteAllText(temp, JsonSerializer.Serialize(progress, new JsonSerializerOptions { WriteIndented = true }));
                File.Move(temp, _path, overwrite: true);   // запись целиком или никак: обрыв посреди записи не портит старый файл
                return true;
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is NotSupportedException)
            {
                _log.Warning("Курс", "Не удалось сохранить прогресс: " + _path, ex);
                return false;
            }
        }
    }
}
