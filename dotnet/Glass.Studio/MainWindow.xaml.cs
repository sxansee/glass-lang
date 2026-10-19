using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Glass.Studio.Editor;
using Glass.Studio.Examples;
using Glass.Studio.Services;
using Glass.Studio.ViewModels;

namespace Glass.Studio
{
    /// <summary>
    /// Окно IDE. Код здесь — только «склейка»: связывает редактор AvalonEdit с ViewModel и
    /// обрабатывает пару событий окна. Вся логика отладчика — в MainViewModel (Glass.Studio.Core).
    /// </summary>
    public partial class MainWindow : Window
    {
        private readonly EditorHost _editorHost;
        private readonly MainViewModel _viewModel;

        public MainWindow()
        {
            InitializeComponent();

            _editorHost = new EditorHost(CodeEditor);
            _viewModel = new MainViewModel(_editorHost, new FileDialogs(this));

            _editorHost.TextEdited += (_, _) => _viewModel.NotifyTextChanged();
            _editorHost.BreakpointToggleRequested += _viewModel.ToggleBreakpoint;
            _editorHost.BreakpointsMoved += _viewModel.ReplaceBreakpoints;
            CodeEditor.TextArea.Caret.PositionChanged += (_, _) => UpdateCaretText();

            DataContext = _viewModel;

            // Не пустой экран при первом запуске: открываем первый пример.
            _viewModel.OpenExample(ExampleCatalog.All[0]);
            UpdateCaretText();
            CodeEditor.Focus();
        }

        private void UpdateCaretText()
        {
            CaretText.Text = "Стр " + CodeEditor.TextArea.Caret.Line + ", стлб " + CodeEditor.TextArea.Caret.Column;
        }

        private void OnClosing(object? sender, CancelEventArgs e)
        {
            if (!_viewModel.ConfirmCanClose())
            {
                e.Cancel = true;
                return;
            }
            _viewModel.Dispose();   // остановит выполняющуюся программу и освободит нативную сессию
        }

        private void OnExit(object sender, RoutedEventArgs e) => Close();

        private void OnAbout(object sender, RoutedEventArgs e)
        {
            MessageBox.Show(this,
                "Glass Studio — среда для языка Glass.\n\n" +
                "Язык, в котором устройство объекта в памяти, виртуальный вызов и сборка мусора видны: " +
                "операторы ~layout, ~vtable, ~address, ~heap, ~gc, а в IDE — пошаговая отладка, схема кучи и инспектор объекта.",
                "О программе", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void OnOutputChanged(object sender, TextChangedEventArgs e)
        {
            if (sender is TextBox box) box.ScrollToEnd();   // вывод всегда прокручен к последней строке
        }

        private void OnHeapMouseWheel(object sender, MouseWheelEventArgs e)
        {
            // Ctrl + колесо — масштаб схемы; без Ctrl колесо прокручивает, как обычно.
            if ((Keyboard.Modifiers & ModifierKeys.Control) == 0) return;
            _viewModel.Zoom += e.Delta > 0 ? 0.1 : -0.1;
            e.Handled = true;
        }
    }
}
