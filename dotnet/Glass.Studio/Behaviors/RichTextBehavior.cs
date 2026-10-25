using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using Glass.Studio.Course;

namespace Glass.Studio.Behaviors
{
    /// <summary>
    /// Прикреплённое свойство Source для TextBlock: показывает RichText (жирный и `код` из мини-разметки уроков) набором Run'ов.
    /// Тексты уроков остаются обычным текстом в ядре и разбираются там же (с тестами), а WPF только раскрашивает.
    /// Использование: &lt;TextBlock beh:RichTextBehavior.Source="{Binding Text}" TextWrapping="Wrap" /&gt;
    /// </summary>
    public static class RichTextBehavior
    {
        public static readonly DependencyProperty SourceProperty = DependencyProperty.RegisterAttached(
            "Source", typeof(RichText), typeof(RichTextBehavior), new PropertyMetadata(null, OnSourceChanged));

        public static RichText? GetSource(DependencyObject element) => (RichText?)element.GetValue(SourceProperty);

        public static void SetSource(DependencyObject element, RichText? value) => element.SetValue(SourceProperty, value);

        private static readonly Brush CodeBackground = Freeze(Color.FromRgb(0xEC, 0xF0, 0xF7));
        private static readonly Brush CodeForeground = Freeze(Color.FromRgb(0x9D, 0x1B, 0x56));

        private static Brush Freeze(Color color)
        {
            var brush = new SolidColorBrush(color);
            brush.Freeze();
            return brush;
        }

        private static void OnSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if (d is not TextBlock block) return;
            block.Inlines.Clear();
            if (e.NewValue is not RichText text) return;

            FontFamily mono = block.TryFindResource("MonoFont") as FontFamily ?? new FontFamily("Consolas");
            foreach (InlineSpan span in text.Spans)
            {
                var run = new Run(span.Text);
                if (span.IsBold) run.FontWeight = FontWeights.SemiBold;
                if (span.IsCode)
                {
                    run.FontFamily = mono;
                    run.Background = CodeBackground;
                    run.Foreground = CodeForeground;
                }
                block.Inlines.Add(run);
            }
        }
    }
}
