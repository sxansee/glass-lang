using System.Collections.Generic;

namespace Glass.Studio.Course
{
    /// <summary>Один урок: теория → пример → задача с автоматической проверкой.</summary>
    /// <param name="ExpectedOutput">Ожидаемый вывод по строкам. Строка с префиксом «re:» — регулярное выражение (для адресов и т.п.).</param>
    /// <param name="RequiredFragments">Что обязано встретиться в тексте решения (например, «for»): задание просит именно этот приём.</param>
    public sealed record Lesson(
        string Id,
        int ChapterNumber,
        int Number,
        string Title,
        string Summary,
        string Theory,
        string Task,
        string Starter,
        string Solution,
        string Hint,
        IReadOnlyList<string> ExpectedOutput,
        IReadOnlyList<string> RequiredFragments)
    {
        public string NumberText => ChapterNumber + "." + Number;
    }

    public sealed record Chapter(int Number, string Title, string Subtitle, IReadOnlyList<Lesson> Lessons);
}
