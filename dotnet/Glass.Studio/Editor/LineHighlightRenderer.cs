using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;
using Glass.Studio.ViewModels;
using ICSharpCode.AvalonEdit.Rendering;

namespace Glass.Studio.Editor
{
    /// <summary>
    /// Подсветка целых строк под текстом: строка выполнения (жёлтая/зелёная/красная) и строки
    /// с ошибками компиляции. Рисуется по образцу CurrentLineHighlightRenderer из AvalonEdit.
    /// </summary>
    public sealed class LineHighlightRenderer : IBackgroundRenderer
    {
        private const double StripeWidth = 4;

        private static readonly Brush CurrentFill = Frozen(Color.FromRgb(0xFF, 0xF3, 0xB0));
        private static readonly Brush CurrentStripe = Frozen(Color.FromRgb(0xF2, 0xC2, 0x00));
        private static readonly Brush CallerFill = Frozen(Color.FromRgb(0xD9, 0xF5, 0xDF));
        private static readonly Brush CallerStripe = Frozen(Color.FromRgb(0x2F, 0xA8, 0x4F));
        private static readonly Brush ErrorFill = Frozen(Color.FromRgb(0xFF, 0xD9, 0xD6));
        private static readonly Brush ErrorStripe = Frozen(Color.FromRgb(0xD6, 0x45, 0x45));
        private static readonly Brush ProblemFill = Frozen(Color.FromRgb(0xFF, 0xEA, 0xE7));

        private readonly HashSet<int> _problemLines = new HashSet<int>();
        private int _executionLine;
        private ExecutionMarker _marker;

        public KnownLayer Layer => KnownLayer.Background;

        public void SetExecution(int? line, ExecutionMarker marker)
        {
            _executionLine = line ?? 0;
            _marker = marker;
        }

        public void SetProblems(IEnumerable<int> lines)
        {
            _problemLines.Clear();
            foreach (int line in lines)
            {
                if (line > 0) _problemLines.Add(line);
            }
        }

        public void Draw(TextView textView, DrawingContext drawingContext)
        {
            foreach (int line in _problemLines)
            {
                if (line != _executionLine) DrawLine(textView, drawingContext, line, ProblemFill, ErrorStripe);
            }

            if (_executionLine <= 0) return;
            switch (_marker)
            {
                case ExecutionMarker.Caller:
                    DrawLine(textView, drawingContext, _executionLine, CallerFill, CallerStripe);
                    break;
                case ExecutionMarker.Error:
                    DrawLine(textView, drawingContext, _executionLine, ErrorFill, ErrorStripe);
                    break;
                default:
                    DrawLine(textView, drawingContext, _executionLine, CurrentFill, CurrentStripe);
                    break;
            }
        }

        private static void DrawLine(TextView textView, DrawingContext drawingContext, int line, Brush fill, Brush stripe)
        {
            VisualLine? visualLine = textView.GetVisualLine(line);
            if (visualLine == null) return;   // строки нет на экране (прокручена или документ короче)

            double top = visualLine.VisualTop - textView.ScrollOffset.Y;
            drawingContext.DrawRectangle(fill, null, new Rect(0, top, textView.ActualWidth, visualLine.Height));
            drawingContext.DrawRectangle(stripe, null, new Rect(0, top, StripeWidth, visualLine.Height));
        }

        private static Brush Frozen(Color color)
        {
            var brush = new SolidColorBrush(color);
            brush.Freeze();
            return brush;
        }
    }
}
