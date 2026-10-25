using System;
using System.IO;

namespace Glass.Studio
{
    /// <summary>
    /// Где приложение хранит свои данные: журнал и прогресс курса. Всё лежит в %LOCALAPPDATA%\GlassStudio —
    /// в профиле пользователя, права администратора не нужны, а удаление папки возвращает приложение к чистому состоянию.
    /// </summary>
    public static class AppPaths
    {
        public const string FolderName = "GlassStudio";

        /// <summary>%LOCALAPPDATA%\GlassStudio (на других системах — каталог данных пользователя).</summary>
        public static string DataDirectory
        {
            get
            {
                string root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                if (string.IsNullOrEmpty(root)) root = Path.GetTempPath();   // профиль недоступен — лучше временная папка, чем падение
                return Path.Combine(root, FolderName);
            }
        }

        /// <summary>Файлы журнала (по файлу на день, хранятся 14 дней).</summary>
        public static string LogDirectory => Path.Combine(DataDirectory, "logs");

        /// <summary>Прогресс курса: пройденные уроки, звёзды, последний открытый урок.</summary>
        public static string CourseProgressFile => Path.Combine(DataDirectory, "course-progress.json");
    }
}
