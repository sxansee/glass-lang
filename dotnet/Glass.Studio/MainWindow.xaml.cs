using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Glass.Studio.Editor;
using Glass.Studio.Examples;
using Glass.Studio.Logging;
using Glass.Studio.Services;
using Glass.Studio.ViewModels;
using ICSharpCode.AvalonEdit.Rendering;

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
            _viewModel = new MainViewModel(_editorHost, new FileDialogs(this), log: AppServices.Log,
                                           settings: new UserSettingsStore(AppPaths.SettingsFile, AppServices.Log));

            // Вкладка «Журнал»: записи приходят из любых потоков, а список живёт в потоке интерфейса — отсюда контекст синхронизации.
            var logPanel = new LogPanelViewModel(AppServices.Log, new DispatcherSynchronizationContext(Dispatcher))
            {
                OpenFolder = AppServices.OpenFolder,
            };
            logPanel.Entries.CollectionChanged += (_, _) => ScrollLogToEnd();
            _viewModel.LogPanel = logPanel;

            _editorHost.TextEdited += (_, _) => _viewModel.NotifyTextChanged();
            _editorHost.TextEdited += (_, _) => _viewModel.InfoCard.Close();   // текст изменился — подсказка по старым смещениям устарела
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

        // ------------------------------------------------------------------ размер шрифта и перетаскивание файла

        // Ctrl + колесо над редактором — размер шрифта (показ на проекторе, чтение с экрана). Без Ctrl колесо прокручивает.
        private void OnEditorMouseWheel(object sender, MouseWheelEventArgs e)
        {
            if (Keyboard.Modifiers != ModifierKeys.Control || e.Delta == 0) return;
            _viewModel.EditorFontSize += e.Delta > 0 ? 1 : -1;
            e.Handled = true;
        }

        // Файл, брошенный на окно, открывается как через «Файл → Открыть»: те же вопросы о несохранённых изменениях.
        private void OnPreviewDragOver(object sender, DragEventArgs e)
        {
            if (!e.Data.GetDataPresent(DataFormats.FileDrop)) return;   // перетаскивание текста внутри редактора — не наше
            e.Effects = DragDropEffects.Copy;
            e.Handled = true;
        }

        private void OnPreviewDrop(object sender, DragEventArgs e)
        {
            if (!e.Data.GetDataPresent(DataFormats.FileDrop)) return;
            e.Handled = true;
            if (e.Data.GetData(DataFormats.FileDrop) is string[] files && files.Length > 0) _viewModel.OpenPath(files[0]);
        }

        // ------------------------------------------------------------------ карточка элемента кода

        // Правая кнопка мыши над словом открывает карточку: что это за элемент, пример, ссылка на документацию и
        // действия редактора (вместо стандартного контекстного меню). Отпускание кнопки, а не нажатие — как у меню.
        private void OnEditorRightClick(object sender, MouseButtonEventArgs e)
        {
            var position = CodeEditor.GetPositionFromPoint(e.GetPosition(CodeEditor));
            if (position == null) return;   // поле точек останова и пустое место вне текста — стандартное поведение
            int offset = CodeEditor.Document.GetOffset(position.Value.Location);
            // Без выделения курсор переезжает под указатель: «Вставить» и «Точка останова» относятся к этой строке.
            if (CodeEditor.SelectionLength == 0) CodeEditor.CaretOffset = offset;
            ShowCard(offset, e.GetPosition(CodeEditor));
            e.Handled = true;
        }

        // F1 — та же карточка для элемента под курсором (клавиатурный путь: мышь не нужна). Esc закрывает её.
        private void OnEditorPreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape && CardPopup.IsOpen)
            {
                CloseCard();
                e.Handled = true;
                return;
            }
            if (e.Key != Key.F1) return;

            var view = CodeEditor.TextArea.TextView;
            Point caret = view.GetVisualPosition(CodeEditor.TextArea.Caret.Position, VisualYPosition.LineBottom) - view.ScrollOffset;
            ShowCard(CodeEditor.CaretOffset, view.TranslatePoint(caret, CodeEditor));
            e.Handled = true;
        }

        private void ShowCard(int offset, Point anchor)
        {
            CardPopup.PlacementTarget = CodeEditor;
            CardPopup.HorizontalOffset = anchor.X;
            CardPopup.VerticalOffset = anchor.Y + 4;
            _viewModel.InfoCard.Open(CodeEditor.Text, offset);
        }

        private void CloseCard()
        {
            _viewModel.InfoCard.Close();
            CodeEditor.Focus();
        }

        private void OnCardKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Escape) return;
            CloseCard();
            e.Handled = true;
        }

        private void OnCardOpenDocs(object sender, RoutedEventArgs e)
        {
            string? url = _viewModel.InfoCard.DocUrl;
            if (url != null) AppServices.OpenUrl(url);
            CloseCard();
        }

        private void OnCardCopyExample(object sender, RoutedEventArgs e)
        {
            string example = _viewModel.InfoCard.Example;
            CloseCard();
            if (example.Length == 0) return;
            try
            {
                Clipboard.SetText(example);
            }
            catch (Exception ex)
            {
                // Буфер обмена мог быть занят другой программой — это не повод ронять IDE.
                AppServices.Log.Error("Редактор", "Не удалось скопировать пример в буфер обмена", ex);
            }
        }

        private void OnCardCut(object sender, RoutedEventArgs e)
        {
            CloseCard();
            CodeEditor.Cut();
        }

        private void OnCardCopy(object sender, RoutedEventArgs e)
        {
            CloseCard();
            CodeEditor.Copy();
        }

        private void OnCardPaste(object sender, RoutedEventArgs e)
        {
            CloseCard();
            CodeEditor.Paste();
        }

        private void OnCardToggleBreakpoint(object sender, RoutedEventArgs e)
        {
            CloseCard();
            _viewModel.ToggleBreakpointCommand.Execute(null);   // строка — под курсором: при правой кнопке он уже переехал
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
