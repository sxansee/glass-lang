using System.Reflection;

namespace Glass.Studio
{
    /// <summary>Сведения о приложении в одном месте (окно «О программе», карточка элемента кода, журнал).</summary>
    public static class AppInfo
    {
        public const string Name = "Glass Studio";

        /// <summary>
        /// Адрес репозитория на GitHub. Ссылки на документацию строятся от него; README, бейджи и
        /// dotnet/Directory.Build.props должны указывать на тот же адрес (это проверяет тест «репозиторий: …»).
        /// </summary>
        public const string RepositoryUrl = "https://github.com/sxansee/glass-lang";

        /// <summary>Каталог документации в репозитории (ветка main).</summary>
        public const string DocsUrl = RepositoryUrl + "/blob/main/docs/";

        public static string Version
        {
            get
            {
                string? text = typeof(AppInfo).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
                if (string.IsNullOrEmpty(text)) return typeof(AppInfo).Assembly.GetName().Version?.ToString(3) ?? "0.0.0";
                int plus = text.IndexOf('+');
                return plus > 0 ? text.Substring(0, plus) : text;   // «1.0.0+abcdef» → «1.0.0»
            }
        }
    }
}
