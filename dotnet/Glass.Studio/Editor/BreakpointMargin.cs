using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using ICSharpCode.AvalonEdit.Editing;
using ICSharpCode.AvalonEdit.Rendering;

namespace Glass.Studio.Editor
{
    /// <summary>
    /// Левое поле редактора («желоб»): красные точки точек останова, при наведении — бледная
    /// подсказка, клик — поставить или снять. Устроено по образцу LineNumberMargin из самого
    /// AvalonEdit: перерисовывается на VisualLinesChanged (прокрутка, правка, перенос строк).
    /// </summary>
    public sealed class BreakpointMargin : AbstractMargin
    {
        public const double MarginWidth = 20;
        private const double DotRadius = 6;

        private static readonly Brush DotBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xE0, 0x31, 0x31)));
        private static readonly Brush HoverBrush = Frozen(new SolidColorBrush(Color.FromArgb(0x55, 0xE0, 0x31, 0x31)));
        private static readonly Brush BackgroundBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xF4, 0xF6, 0xFA)));

        private HashSet<int> _breakpoints = new HashSet<int>();
        private int _hoverLine;

        /// <summary>Пользователь кликнул по строке (номер с 1) — переключить точку останова.</summary>
        public event Action<int>? ToggleRequested;

        public void SetBreakpoints(IEnumerable<int> lines)
        {
            _breakpoints = new HashSet<int>(lines);   // копия: источник — живой набор ViewModel
            InvalidateVisual();
        }

        protected override Size MeasureOverride(Size availableSize) => new Size(MarginWidth, 0);

        protected override void OnTextViewChanged(TextView oldTextView, TextView newTextView)
        {
            if (oldTextView != null) oldTextView.VisualLinesChanged -= OnVisualLinesChanged;
            base.OnTextViewChanged(oldTextView, newTextView);
            if (newTextView != null) newTextView.VisualLinesChanged += OnVisualLinesChanged;
            InvalidateVisual();
        }

        private void OnVisualLinesChanged(object? sender, EventArgs e) => InvalidateVisual();

        protected override void OnRender(DrawingContext drawingContext)
        {
            Size size = RenderSize;
            // Фон — не только для вида: элемент без закрашенной области не получает щелчки мыши.
            drawingContext.DrawRectangle(BackgroundBrush, null, new Rect(0, 0, size.Width, size.Height));

            TextView textView = TextView;
            if (textView == null || !textView.VisualLinesValid) return;

            foreach (VisualLine line in textView.VisualLines)
            {
                int number = line.FirstDocumentLine.LineNumber;
                bool isBreakpoint = _breakpoints.Contains(number);
                if (!isBreakpoint && number != _hoverLine) continue;

                double middle = line.GetTextLineVisualYPosition(line.TextLines[0], VisualYPosition.LineMiddle) - textView.VerticalOffset;
                drawingContext.DrawEllipse(isBreakpoint ? DotBrush : HoverBrush, null,
                    new Point(size.Width / 2, middle), DotRadius, DotRadius);
            }
        }

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonDown(e);
            int line = LineAt(e.GetPosition(this));
            if (line <= 0) return;
            e.Handled = true;
            ToggleRequested?.Invoke(line);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            int line = LineAt(e.GetPosition(this));
            if (line == _hoverLine) return;
            _hoverLine = line;
            InvalidateVisual();
        }

        protected override void OnMouseLeave(MouseEventArgs e)
        {
            base.OnMouseLeave(e);
            if (_hoverLine == 0) return;
            _hoverLine = 0;
            InvalidateVisual();
        }

        private int LineAt(Point position)
        {
            TextView textView = TextView;
            if (textView == null || !textView.VisualLinesValid) return 0;
            VisualLine? line = textView.GetVisualLineFromVisualTop(position.Y + textView.VerticalOffset);
            return line?.FirstDocumentLine.LineNumber ?? 0;
        }

        private static Brush Frozen(SolidColorBrush brush)
        {
            brush.Freeze();
            return brush;
        }
    }
}
