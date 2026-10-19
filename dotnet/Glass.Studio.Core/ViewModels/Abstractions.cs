// Границы между переносимым ядром IDE и тем, что бывает только в окне:
// редактор кода и файловые диалоги. WPF-оболочка реализует эти интерфейсы
// поверх AvalonEdit и Microsoft.Win32; тесты — простыми заглушками. Благодаря
// этому вся логика отладчика (когда какая кнопка доступна, что показывать на
// паузе, что делать при ошибке) проверяется без Windows.

using System.Collections.Generic;
using Glass.Studio.Models;

namespace Glass.Studio.ViewModels
{
    public enum IdeState
    {
        /// <summary>Сессии нет — можно редактировать и запускать.</summary>
        Editing,
        /// <summary>Команда отладчика выполняется (UI жив, доступна только «Стоп»).</summary>
        Running,
        /// <summary>Программа на паузе — виден стек, переменные, куча.</summary>
        Paused,
        /// <summary>Программа закончилась (сессия сохранена: вывод, ошибка, состояние на момент сбоя).</summary>
        Finished,
    }

    public enum ExecutionMarker
    {
        /// <summary>Строка, которая выполнится следующей (жёлтая, стрелка).</summary>
        Current,
        /// <summary>Строка вызова в выбранном фрейме стека (зелёная).</summary>
        Caller,
        /// <summary>Строка, на которой упала программа (красная).</summary>
        Error,
    }

    public interface IEditorHost
    {
        string GetText();
        void SetText(string text);

        /// <summary>Номер строки (с 1), на которой стоит курсор.</summary>
        int CaretLine { get; }

        /// <summary>Подсветить строку выполнения и прокрутить к ней; null — убрать подсветку.</summary>
        void ShowExecutionLine(int? line, ExecutionMarker marker);

        /// <summary>Подчеркнуть строки с ошибками (пустой список — снять).</summary>
        void ShowProblems(IReadOnlyList<Diagnostic> problems);

        /// <summary>Перерисовать красные точки точек останова.</summary>
        void ShowBreakpoints(IReadOnlyCollection<int> lines);

        void GoToLine(int line);
    }

    public interface IFileDialogs
    {
        /// <summary>Путь выбранного файла или null, если пользователь отказался.</summary>
        string? ChooseFileToOpen();

        string? ChooseFileToSave(string suggestedName);

        /// <summary>«Есть несохранённые изменения — продолжить?»</summary>
        bool ConfirmDiscardChanges();
    }
}
