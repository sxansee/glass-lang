using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Media;
using Glass.Studio.Models;
using Glass.Studio.ViewModels;
using ICSharpCode.AvalonEdit;
using ICSharpCode.AvalonEdit.Document;
using ICSharpCode.AvalonEdit.Rendering;

namespace Glass.Studio.Editor
{
    /// <summary>
    /// Реализация IEditorHost поверх AvalonEdit: единственное место, где ViewModel «видит» редактор.
    /// Точки останова привязаны к строкам через TextAnchor — вставили строку выше, и точка уехала
    /// вниз вместе со своей строкой (как в Visual Studio), а не осталась на прежнем номере.
    /// </summary>
    public sealed class EditorHost : IEditorHost
    {
        private readonly TextEditor _editor;
        private readonly LineHighlightRenderer _highlights = new LineHighlightRenderer();
        private readonly BreakpointMargin _margin = new BreakpointMargin();
        private readonly List<TextAnchor> _anchors = new List<TextAnchor>();
        private List<int> _shownBreakpoints = new List<int>();

        public EditorHost(TextEditor editor)
        {
            _editor = editor ?? throw new ArgumentNullException(nameof(editor));

            _editor.SyntaxHighlighting = GlassHighlighting.Load();
            _editor.TextArea.TextView.BackgroundRenderers.Add(_highlights);
            _editor.TextArea.LeftMargins.Insert(0, _margin);   // левее номеров строк
            _margin.ToggleRequested += line => BreakpointToggleRequested?.Invoke(line);
            _editor.TextChanged += OnTextChanged;

            // Табы заменяем пробелами и показываем отступ в 4 пробела — так выглядят примеры.
            _editor.Options.ConvertTabsToSpaces = true;
            _editor.Options.IndentationSize = 4;
            _editor.Options.HighlightCurrentLine = false;   // жёлтую строку выполнения не путать с «текущей строкой»
            _editor.Options.EnableHyperlinks = false;
            _editor.Options.EnableEmailHyperlinks = false;
        }

        /// <summary>Текст изменён пользователем (или программно).</summary>
        public event EventHandler? TextEdited;

        /// <summary>Клик по полю точек останова — ViewModel решает, ставить или снимать.</summary>
        public event Action<int>? BreakpointToggleRequested;

        /// <summary>Точки останова сдвинулись вслед за правкой текста — новый полный набор строк.</summary>
        public event Action<IReadOnlyList<int>>? BreakpointsMoved;

        // ------------------------------------------------------------------ IEditorHost

        public string GetText() => _editor.Text;

        public void SetText(string text)
        {
            _editor.Text = text;
            _editor.CaretOffset = 0;
            _editor.ScrollToHome();
        }

        public int CaretLine => _editor.TextArea.Caret.Line;

        public void ShowExecutionLine(int? line, ExecutionMarker marker)
        {
            int? valid = line.HasValue && line.Value >= 1 && line.Value <= _editor.Document.LineCount ? line : null;
            _highlights.SetExecution(valid, marker);
            Redraw();
            if (valid.HasValue) _editor.ScrollToLine(valid.Value);
        }

        public void ShowProblems(IReadOnlyList<Diagnostic> problems)
        {
            _highlights.SetProblems(problems.Where(p => p.Kind == DiagnosticKind.Compile).Select(p => p.Line));
            Redraw();
        }

        public void ShowBreakpoints(IReadOnlyCollection<int> lines)
        {
            int lineCount = _editor.Document.LineCount;
            List<int> wanted = lines.Where(l => l >= 1 && l <= lineCount).Distinct().OrderBy(l => l).ToList();
            if (!wanted.SequenceEqual(AnchoredLines()) || _anchors.Count != wanted.Count)
            {
                _anchors.Clear();
                foreach (int line in wanted)
                {
                    TextAnchor anchor = _editor.Document.CreateAnchor(_editor.Document.GetLineByNumber(line).Offset);
                    anchor.MovementType = AnchorMovementType.AfterInsertion;   // остаётся с текстом строки при вставке перед ней
                    _anchors.Add(anchor);
                }
            }
            _shownBreakpoints = wanted;
            _margin.SetBreakpoints(wanted);
        }

        public void GoToLine(int line)
        {
            int target = Math.Max(1, Math.Min(line, _editor.Document.LineCount));
            _editor.TextArea.Caret.Line = target;
            _editor.TextArea.Caret.Column = 1;
            _editor.TextArea.Caret.BringCaretToView();
            _editor.ScrollToLine(target);
            _editor.Focus();
        }

        // ------------------------------------------------------------------ внутреннее

        private void OnTextChanged(object? sender, EventArgs e)
        {
            if (_anchors.Count > 0)
            {
                List<int> moved = AnchoredLines();
                if (!moved.SequenceEqual(_shownBreakpoints))
                {
                    _shownBreakpoints = moved;
                    _margin.SetBreakpoints(moved);
                    BreakpointsMoved?.Invoke(moved);
                }
            }
            TextEdited?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>Текущие номера строк живых якорей (якорь удалённой строки не считается).</summary>
        private List<int> AnchoredLines() =>
            _anchors.Where(a => !a.IsDeleted).Select(a => a.Line).Distinct().OrderBy(l => l).ToList();

        private void Redraw() => _editor.TextArea.TextView.InvalidateLayer(KnownLayer.Background);
    }
}
