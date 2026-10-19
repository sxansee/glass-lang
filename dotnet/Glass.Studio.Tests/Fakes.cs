using System.Collections.Generic;
using System.Linq;
using Glass.Studio.Models;
using Glass.Studio.ViewModels;

namespace Glass.Studio.Tests
{
    /// <summary>Редактор-заглушка: запоминает всё, что ViewModel ему «показала».</summary>
    public sealed class FakeEditor : IEditorHost
    {
        public string Text = "";
        public int Caret = 1;
        public int? ExecutionLine;
        public ExecutionMarker Marker;
        public List<Diagnostic> Problems = new List<Diagnostic>();
        public SortedSet<int> Breakpoints = new SortedSet<int>();
        public int? LastGoTo;
        public int SetTextCalls;

        public string GetText() => Text;
        public void SetText(string text) { Text = text; SetTextCalls++; }
        public int CaretLine => Caret;

        public void ShowExecutionLine(int? line, ExecutionMarker marker)
        {
            ExecutionLine = line;
            Marker = marker;
        }

        public void ShowProblems(IReadOnlyList<Diagnostic> problems) => Problems = problems.ToList();

        public void ShowBreakpoints(IReadOnlyCollection<int> lines) => Breakpoints = new SortedSet<int>(lines);

        public void GoToLine(int line) => LastGoTo = line;
    }

    public sealed class FakeDialogs : IFileDialogs
    {
        public string? OpenPath;
        public string? SavePath;
        public bool Confirm = true;
        public int ConfirmCalls;

        public string? ChooseFileToOpen() => OpenPath;
        public string? ChooseFileToSave(string suggestedName) => SavePath;

        public bool ConfirmDiscardChanges()
        {
            ConfirmCalls++;
            return Confirm;
        }
    }
}
