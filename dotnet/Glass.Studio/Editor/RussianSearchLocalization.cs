using ICSharpCode.AvalonEdit.Search;

namespace Glass.Studio.Editor
{
    /// <summary>Подписи встроенной панели поиска AvalonEdit по-русски (по умолчанию — английские).</summary>
    public sealed class RussianSearchLocalization : Localization
    {
        public override string MatchCaseText => "Учитывать регистр";
        public override string MatchWholeWordsText => "Слово целиком";
        public override string UseRegexText => "Регулярное выражение";
        public override string FindNextText => "Найти далее (F3)";
        public override string FindPreviousText => "Найти ранее (Shift+F3)";
        public override string ErrorText => "Ошибка: ";
        public override string NoMatchesFoundText => "Совпадений не найдено";
    }
}
