using System;
using System.Diagnostics;
using Glass.Studio.Course;
using Glass.Studio.Logging;
using Glass.Studio.ViewModels;

namespace Glass.Studio.Services
{
    /// <summary>
    /// Общие службы приложения: единый журнал и фабрика курса. Пути — в AppPaths (%LOCALAPPDATA%\GlassStudio), чтобы их
    /// можно было проверить тестами без Windows; здесь только то, что требует процесса и оболочки.
    /// </summary>
    public static class AppServices
    {
        private static readonly object Gate = new object();
        private static AppLog? _log;

        /// <summary>Журнал приложения: файлы лежат в AppPaths.LogDirectory. Создаётся при первом обращении.</summary>
        public static AppLog Log
        {
            get
            {
                lock (Gate)
                {
                    return _log ??= new AppLog(AppPaths.LogDirectory);
                }
            }
        }

        /// <summary>Курс на общем журнале и общем файле прогресса. host — окно IDE (редактор), откуда курс берёт код ученика.</summary>
        public static CourseViewModel CreateCourse(ICourseHost host) =>
            new CourseViewModel(new CourseProgressStore(AppPaths.CourseProgressFile, Log), host, Log);

        public static void OpenFolder(string path) => Open(path);

        public static void OpenUrl(string url) => Open(url);

        private static void Open(string target)
        {
            try
            {
                Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                Log.Error("Приложение", "Не удалось открыть «" + target + "»", ex);
            }
        }
    }
}
