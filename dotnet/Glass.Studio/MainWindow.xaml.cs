using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
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
        private CourseWindow? _courseWindow;

        public MainWindow()
        {
            InitializeComponent();

            _editorHost = new EditorHost(CodeEditor);
            _viewModel = new MainViewModel(_editorHost, new FileDialogs(this), log: AppServices.Log);

            // Вкладка «Журнал»: записи приходят из любых потоков, а список живёт в потоке интерфейса — отсюда контекст синхронизации.
            var logPanel = new LogPanelViewModel(AppServices.Log, new DispatcherSynchronizationContext(Dispatcher))
            {
                OpenFolder = AppServices.OpenFolder,
            };
            logPanel.Entries.CollectionChanged += (_, _) => ScrollLogToEnd();
            _viewModel.LogPanel = logPanel;

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

        private void ScrollLogToEnd()
        {
            // Журнал растёт снизу: показываем последнюю запись, как в консоли.
            if (LogList.Items.Count > 0) LogList.ScrollIntoView(LogList.Items[LogList.Items.Count - 1]);
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

        private void OnOpenCourse(object sender, RoutedEventArgs e)
        {
            // Окно курса одно: повторное нажатие возвращает уже открытое, а не плодит копии.
            if (_courseWindow != null)
            {
                if (_courseWindow.WindowState == WindowState.Minimized) _courseWindow.WindowState = WindowState.Normal;
                _courseWindow.Activate();
                return;
            }

            _courseWindow = new CourseWindow(AppServices.CreateCourse(_viewModel)) { Owner = this };
            _courseWindow.Closed += (_, _) => _courseWindow = null;
            _courseWindow.Show();
        }

        private void OnOpenLogFolder(object sender, RoutedEventArgs e) => AppServices.OpenFolder(AppPaths.LogDirectory);

        private void OnOpenProjectPage(object sender, RoutedEventArgs e) => AppServices.OpenUrl(AppInfo.RepositoryUrl);

        private void OnAbout(object sender, RoutedEventArgs e)
        {
            MessageBox.Show(this,
                AppInfo.Name + " " + AppInfo.Version + " — среда для языка Glass.\n\n" +
                "Язык, в котором устройство объекта в памяти, виртуальный вызов и сборка мусора видны: " +
                "операторы ~layout, ~vtable, ~address, ~heap, ~gc, а в IDE — пошаговая отладка, схема кучи, " +
                "инспектор объекта и интерактивный курс.\n\n" +
                "Исходный код и документация: " + AppInfo.RepositoryUrl + "\n" +
                "Журнал и прогресс курса: " + AppPaths.DataDirectory,
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
