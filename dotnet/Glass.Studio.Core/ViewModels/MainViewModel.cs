// Главная ViewModel IDE: состояние отладчика, команды, всё, что показывают панели.
//
// Состояния (IdeState): Editing → Running → Paused ⇄ Running → Finished.
// Правило потоков: команды выполнения идут на пуле потоков (см. DebugSession),
// а ViewModel «просыпается» после await на потоке интерфейса (SynchronizationContext
// WPF), поэтому все свойства меняются только в UI-потоке. Тесты вызывают те же
// async-методы без контекста — логика та же.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Glass.Interop;
using Glass.Studio.Course;
using Glass.Studio.Debugging;
using Glass.Studio.Examples;
using Glass.Studio.Heap;
using Glass.Studio.Logging;
using Glass.Studio.Models;
using Glass.Studio.Mvvm;

namespace Glass.Studio.ViewModels
{
    public sealed class MainViewModel : ObservableObject, IDisposable, ICourseHost
    {
        /// <summary>Локальные переменные грузятся только для верхних N фреймов — глубокая рекурсия не тормозит каждую паузу.</summary>
        public const int MaxFramesWithLocals = 40;

        private const string AppName = "Glass Studio";

        private readonly IEditorHost _editor;
        private readonly IFileDialogs _dialogs;
        private readonly int _liveCheckDelayMs;
        private readonly ILog _log;
        private readonly SortedSet<int> _breakpoints = new SortedSet<int>();

        private DebugSession? _session;
        private CancellationTokenSource? _liveCts;
        private bool _suppressTextChanged;

        private IdeState _state = IdeState.Editing;
        private string _statusText = "Готово";
        private string _output = "";
        private int _selectedBottomTab;
        private IReadOnlyList<ProblemItem> _problems = Array.Empty<ProblemItem>();
        private IReadOnlyList<StackFrameItem> _callStack = Array.Empty<StackFrameItem>();
        private IReadOnlyList<LocalItem> _locals = Array.Empty<LocalItem>();
        private StackFrameItem? _selectedFrame;
        private LocalItem? _selectedLocal;
        private IReadOnlyList<FrameLocals> _frames = Array.Empty<FrameLocals>();
        private HeapSnapshot _heap = HeapSnapshot.Empty;
        private HeapSnapshot? _previousHeap;
        private bool _hasHeap;
        private HeapGraphModel _heapGraph = HeapGraphModel.Empty;
        private bool _isHeapStale;
        private string? _selectedAddress;
        private bool _isSourceStale;
        private string? _currentFilePath;
        private bool _isModified;
        private double _zoom = 1.0;
        private string _gcMessage = "";

        /// <param name="liveCheckDelayMs">Пауза после ввода перед фоновой проверкой кода; отрицательное — проверка отключена (тесты).</param>
        public MainViewModel(IEditorHost editor, IFileDialogs dialogs, int liveCheckDelayMs = 600, ILog? log = null)
        {
            _log = log ?? NullLog.Instance;
            _editor = editor ?? throw new ArgumentNullException(nameof(editor));
            _dialogs = dialogs ?? throw new ArgumentNullException(nameof(dialogs));
            _liveCheckDelayMs = liveCheckDelayMs;

            Inspector = new ObjectInspectorViewModel();
            Examples = ExampleCatalog.All;
            ExampleMenu = ExampleCatalog.All.Select(e => new ExampleMenuItem(e, OpenExample, () => State != IdeState.Running)).ToList();

            StartOrContinueCommand = new AsyncRelayCommand(StartOrContinueAsync, () => State != IdeState.Running, OnCommandError);
            RunWithoutDebuggingCommand = new AsyncRelayCommand(RunWithoutDebuggingAsync,
                () => State == IdeState.Editing || State == IdeState.Finished, OnCommandError);
            StepIntoCommand = new AsyncRelayCommand(StepIntoAsync, () => State != IdeState.Running, OnCommandError);
            StepOverCommand = new AsyncRelayCommand(StepOverAsync, () => State != IdeState.Running, OnCommandError);
            StepOutCommand = new AsyncRelayCommand(StepOutAsync, () => State == IdeState.Paused, OnCommandError);
            StopCommand = new RelayCommand(Stop, () => State != IdeState.Editing);
            CollectGarbageCommand = new RelayCommand(() => CollectGarbage(), () => State == IdeState.Paused);
            ToggleBreakpointCommand = new RelayCommand(() => ToggleBreakpoint(_editor.CaretLine));
            ClearBreakpointsCommand = new RelayCommand(ClearBreakpoints, () => _breakpoints.Count > 0);
            NewCommand = new RelayCommand(NewFile, () => State != IdeState.Running);
            OpenCommand = new RelayCommand(OpenFile, () => State != IdeState.Running);
            SaveCommand = new RelayCommand(() => Save(), null);
            SaveAsCommand = new RelayCommand(() => SaveAs(), null);
            OpenExampleCommand = new RelayCommand(p => { if (p is ExampleItem item) OpenExample(item); },
                _ => State != IdeState.Running);
            SelectHeapNodeCommand = new RelayCommand(p => SelectHeapNode(p as string));
            ZoomInCommand = new RelayCommand(() => Zoom = Math.Min(2.0, Zoom + 0.1));
            ZoomOutCommand = new RelayCommand(() => Zoom = Math.Max(0.4, Zoom - 0.1));
            ResetZoomCommand = new RelayCommand(() => Zoom = 1.0);
        }

        // ------------------------------------------------------------------
        // Состояние
        // ------------------------------------------------------------------

        public IdeState State
        {
            get => _state;
            private set
            {
                if (!SetProperty(ref _state, value)) return;
                OnPropertyChanged(nameof(IsRunning));
                OnPropertyChanged(nameof(IsPaused));
                OnPropertyChanged(nameof(IsEditing));
                OnPropertyChanged(nameof(IsFinished));
                OnPropertyChanged(nameof(HasSession));
                RaiseAllCanExecuteChanged();
            }
        }

        public bool IsRunning => State == IdeState.Running;
        public bool IsPaused => State == IdeState.Paused;
        public bool IsEditing => State == IdeState.Editing;
        public bool IsFinished => State == IdeState.Finished;
        public bool HasSession => State != IdeState.Editing;

        public string StatusText { get => _statusText; private set => SetProperty(ref _statusText, value); }

        /// <summary>Весь вывод print() программы.</summary>
        public string Output { get => _output; private set => SetProperty(ref _output, value); }

        /// <summary>0 — «Вывод», 1 — «Ошибки», 2 — «Журнал».</summary>
        public int SelectedBottomTab { get => _selectedBottomTab; set => SetProperty(ref _selectedBottomTab, value); }

        public IReadOnlyList<ProblemItem> Problems
        {
            get => _problems;
            private set
            {
                if (!SetProperty(ref _problems, value)) return;
                OnPropertyChanged(nameof(HasProblems));
                OnPropertyChanged(nameof(ProblemsHeader));
            }
        }

        public bool HasProblems => Problems.Count > 0;
        public string ProblemsHeader => Problems.Count == 0 ? "Ошибки" : "Ошибки (" + Problems.Count.ToString(CultureInfo.InvariantCulture) + ")";

        private ProblemItem? _selectedProblem;
        public ProblemItem? SelectedProblem
        {
            get => _selectedProblem;
            set
            {
                if (!SetProperty(ref _selectedProblem, value) || value == null) return;
                if (value.Line > 0) _editor.GoToLine(value.Line);
            }
        }

        // ------------------------------------------------------------------
        // Стек, переменные, куча, инспектор
        // ------------------------------------------------------------------

        public IReadOnlyList<StackFrameItem> CallStack { get => _callStack; private set => SetProperty(ref _callStack, value); }

        public IReadOnlyList<LocalItem> Locals { get => _locals; private set => SetProperty(ref _locals, value); }

        public StackFrameItem? SelectedFrame
        {
            get => _selectedFrame;
            set
            {
                if (value == null || ReferenceEquals(value, _selectedFrame)) return;  // ListBox сбрасывает выбор при смене списка — игнорируем
                _selectedFrame = value;
                OnPropertyChanged();
                ShowFrame(value);
            }
        }

        public LocalItem? SelectedLocal
        {
            get => _selectedLocal;
            set
            {
                if (!SetProperty(ref _selectedLocal, value) || value == null) return;
                if (value.IsReference) SelectHeapNode(value.Address);
            }
        }

        public HeapGraphModel HeapGraph { get => _heapGraph; private set => SetProperty(ref _heapGraph, value); }

        /// <summary>true — схема показывает последнее состояние ДО завершения программы (после завершения кучи уже нет).</summary>
        public bool IsHeapStale { get => _isHeapStale; private set => SetProperty(ref _isHeapStale, value); }

        public string HeapSummary
        {
            get
            {
                int total = HeapGraph.Nodes.Count + HeapGraph.HiddenNodeCount;
                if (total == 0) return "Куча пуста";
                string text = "В куче: " + total.ToString(CultureInfo.InvariantCulture) + " " + Plural.Choose(total, "объект", "объекта", "объектов");
                if (HeapGraph.GarbageCount > 0) text += " · мусор: " + HeapGraph.GarbageCount.ToString(CultureInfo.InvariantCulture);
                if (HeapGraph.HiddenNodeCount > 0) text += " · не показано: " + HeapGraph.HiddenNodeCount.ToString(CultureInfo.InvariantCulture);
                return text;
            }
        }

        /// <summary>Результат последней сборки мусора («Собрано 2 объекта»); пусто, пока не собирали.</summary>
        public string GcMessage { get => _gcMessage; private set => SetProperty(ref _gcMessage, value); }

        public ObjectInspectorViewModel Inspector { get; }

        /// <summary>Карточка элемента кода (появляется по правой кнопке мыши в редакторе).</summary>
        public CodeInfoCardViewModel InfoCard { get; } = new CodeInfoCardViewModel();

        private LogPanelViewModel? _logPanel;

        /// <summary>Вкладка «Журнал»: создаётся оболочкой (ей нужен контекст синхронизации интерфейса).</summary>
        public LogPanelViewModel? LogPanel
        {
            get => _logPanel;
            set => SetProperty(ref _logPanel, value);
        }

        public double Zoom { get => _zoom; set => SetProperty(ref _zoom, Math.Max(0.4, Math.Min(2.0, value))); }

        // ------------------------------------------------------------------
        // Файл
        // ------------------------------------------------------------------

        public IReadOnlyList<ExampleItem> Examples { get; }

        /// <summary>Те же примеры, но как пункты меню с готовыми командами.</summary>
        public IReadOnlyList<ExampleMenuItem> ExampleMenu { get; }
        public bool IsModified { get => _isModified; private set { if (SetProperty(ref _isModified, value)) OnPropertyChanged(nameof(WindowTitle)); } }

        public string? CurrentFilePath
        {
            get => _currentFilePath;
            private set { if (SetProperty(ref _currentFilePath, value)) OnPropertyChanged(nameof(WindowTitle)); }
        }

        /// <summary>Код изменили после запуска — подсветка строки выполнения может не совпадать с программой.</summary>
        public bool IsSourceStale { get => _isSourceStale; private set => SetProperty(ref _isSourceStale, value); }

        private string? _exampleTitle;

        public string WindowTitle
        {
            get
            {
                string name = CurrentFilePath != null ? Path.GetFileName(CurrentFilePath) : (_exampleTitle ?? "Без имени");
                return (IsModified ? "● " : "") + name + " — " + AppName;
            }
        }

        // ------------------------------------------------------------------
        // Команды
        // ------------------------------------------------------------------

        public ICommand StartOrContinueCommand { get; }
        public ICommand RunWithoutDebuggingCommand { get; }
        public ICommand StepIntoCommand { get; }
        public ICommand StepOverCommand { get; }
        public ICommand StepOutCommand { get; }
        public ICommand StopCommand { get; }
        public ICommand CollectGarbageCommand { get; }
        public ICommand ToggleBreakpointCommand { get; }
        public ICommand ClearBreakpointsCommand { get; }
        public ICommand NewCommand { get; }
        public ICommand OpenCommand { get; }
        public ICommand SaveCommand { get; }
        public ICommand SaveAsCommand { get; }
        public ICommand OpenExampleCommand { get; }
        public ICommand SelectHeapNodeCommand { get; }
        public ICommand ZoomInCommand { get; }
        public ICommand ZoomOutCommand { get; }
        public ICommand ResetZoomCommand { get; }

        // ------------------------------------------------------------------
        // Точки останова
        // ------------------------------------------------------------------

        public IReadOnlyCollection<int> Breakpoints => _breakpoints;

        public void ToggleBreakpoint(int line)
        {
            if (line <= 0) return;
            if (!_breakpoints.Remove(line)) _breakpoints.Add(line);
            ApplyBreakpoints();
        }

        public void ClearBreakpoints()
        {
            _breakpoints.Clear();
            ApplyBreakpoints();
        }

        /// <summary>
        /// Редактор сдвинул точки останова вслед за правкой текста (вставили строку выше — точка
        /// уехала вниз вместе со «своей» строкой). Заменяет весь набор; без изменений — ничего не делает.
        /// </summary>
        public void ReplaceBreakpoints(IEnumerable<int> lines)
        {
            var updated = new SortedSet<int>(lines.Where(l => l > 0));
            if (updated.SetEquals(_breakpoints)) return;
            _breakpoints.Clear();
            foreach (int line in updated) _breakpoints.Add(line);
            ApplyBreakpoints();
        }

        private void ApplyBreakpoints()
        {
            _editor.ShowBreakpoints(_breakpoints);
            _session?.SetBreakpoints(_breakpoints);  // уйдёт в VM на старте следующей команды
            RaiseAllCanExecuteChanged();
        }

        // ------------------------------------------------------------------
        // Выполнение
        // ------------------------------------------------------------------

        private enum Mode { Continue, StepInto, StepOver, StepOut, Run }

        /// <summary>F5: начать отладку (с точками останова) или продолжить с паузы.</summary>
        public Task StartOrContinueAsync() => ExecuteAsync(Mode.Continue);

        /// <summary>F11: шаг с заходом; без сессии — начать и встать на первой строке main().</summary>
        public Task StepIntoAsync() => ExecuteAsync(Mode.StepInto);

        /// <summary>F10.</summary>
        public Task StepOverAsync() => ExecuteAsync(Mode.StepOver);

        /// <summary>Shift+F11: только на паузе.</summary>
        public Task StepOutAsync() => ExecuteAsync(Mode.StepOut);

        /// <summary>Ctrl+F5: выполнить целиком, без остановок.</summary>
        public Task RunWithoutDebuggingAsync() => ExecuteAsync(Mode.Run);

        private async Task ExecuteAsync(Mode mode)
        {
            if (State == IdeState.Running) return;
            if (mode == Mode.StepOut && State != IdeState.Paused) return;
            if (mode == Mode.Run && State == IdeState.Paused) return;

            bool needNewSession = _session == null || State == IdeState.Finished || State == IdeState.Editing;
            if (needNewSession && !TryBeginSession()) return;
            DebugSession session = _session!;

            session.SetBreakpoints(_breakpoints);
            _log.Info("Выполнение", mode == Mode.Run ? "Запуск без отладки" : mode == Mode.Continue ? "Отладка: продолжить до точки останова"
                : mode == Mode.StepInto ? "Шаг с заходом" : mode == Mode.StepOver ? "Шаг с обходом" : "Шаг с выходом");
            State = IdeState.Running;
            StatusText = "Выполняется…";
            _editor.ShowExecutionLine(null, ExecutionMarker.Current);
            SelectedBottomTab = 0;

            CommandResult result;
            try
            {
                switch (mode)
                {
                    case Mode.Run: result = await session.RunAsync(); break;
                    case Mode.StepInto: result = await session.StepIntoAsync(); break;
                    case Mode.StepOver: result = await session.StepOverAsync(); break;
                    case Mode.StepOut: result = await session.StepOutAsync(); break;
                    default: result = await session.ContinueAsync(); break;
                }
            }
            catch (Exception ex)
            {
                EndSession();
                _log.Error("Выполнение", "Команда выполнения завершилась исключением", ex);
                StatusText = "Внутренняя ошибка: " + ex.Message;
                return;
            }

            try
            {
                ApplyResult(session, result);
            }
            catch (Exception ex)
            {
                // Состояние не прочиталось (не должно случаться) — не оставляем интерфейс в
                // полупоказанном виде: закрываем сессию и говорим об этом прямо.
                EndSession();
                _log.Error("Выполнение", "Не удалось прочитать состояние программы", ex);
                StatusText = "Не удалось прочитать состояние программы: " + ex.Message;
            }
        }

        /// <summary>Компилирует текущий код и открывает новую сессию. false — ошибка компиляции (показана в «Ошибках»).</summary>
        private bool TryBeginSession()
        {
            EndSession();
            string source = _editor.GetText();
            DebugSession session;
            try
            {
                session = DebugSession.Compile(source);
            }
            catch (GlassCompileException ex)
            {
                Diagnostic diagnostic = DiagnosticParser.ParseCompileError(ex.Message);
                _log.Info("Компиляция", "Ошибка" + (diagnostic.Line > 0 ? " (строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) + ")" : "") + ": " + diagnostic.Message);
                SetProblems(new[] { diagnostic });
                SelectedBottomTab = 1;
                StatusText = "Ошибка компиляции" + (diagnostic.Line > 0 ? " · строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) : "") +
                             ": " + diagnostic.Message;
                if (diagnostic.Line > 0) _editor.GoToLine(diagnostic.Line);
                return false;
            }

            _session = session;
            _log.Debug("Компиляция", "Программа скомпилирована");
            SetProblems(Array.Empty<Diagnostic>());
            Output = "";
            GcMessage = "";
            IsSourceStale = false;
            return true;
        }

        private void ApplyResult(DebugSession session, CommandResult result)
        {
            if (!ReferenceEquals(session, _session)) return;  // за время выполнения сессию закрыли («Стоп» после паузы и т.п.)

            if (result.Phase == SessionPhase.Paused)
            {
                State = IdeState.Paused;
                DebugSnapshot snapshot = session.Capture(MaxFramesWithLocals);
                ApplySnapshot(snapshot, ExecutionMarker.Current);
                string where = "строка " + snapshot.CurrentLine.ToString(CultureInfo.InvariantCulture);
                _log.Debug("Выполнение", "Пауза, " + where + ", объектов в куче: " + snapshot.Heap.Objects.Count.ToString(CultureInfo.InvariantCulture));
                StatusText = result.Status == GlassRunStatus.PausedAtBreakpoint
                    ? "Остановлено на точке останова · " + where
                    : "Пауза · " + where;
                return;
            }

            // Программа закончилась.
            State = IdeState.Finished;
            Output = session.GetOutput();
            if (result.RuntimeError == null)
            {
                _editor.ShowExecutionLine(null, ExecutionMarker.Current);
                IsHeapStale = _hasHeap;
                _log.Info("Выполнение", "Программа завершена");
                StatusText = "Программа завершена";
                return;
            }

            // Ошибка выполнения / «Стоп»: VM сохраняет состояние на момент сбоя — показываем
            // его как есть (стек, переменные, куча), чтобы можно было разобраться, ПОЧЕМУ.
            Diagnostic diagnostic = DiagnosticParser.ParseRuntimeError(result.RuntimeError);
            DebugSnapshot? crash = null;
            try { crash = session.Capture(MaxFramesWithLocals); } catch (InvalidOperationException) { }
            if (crash != null && crash.CallStack.Count > 0)
            {
                ApplySnapshot(crash, ExecutionMarker.Error);
                if (diagnostic.Line <= 0 && crash.CurrentLine > 0)
                {
                    diagnostic = diagnostic with { Line = crash.CurrentLine };
                }
            }
            else
            {
                IsHeapStale = _hasHeap;
            }

            SetProblems(new[] { diagnostic });
            _log.Info("Выполнение", (result.StoppedByUser ? "Остановлено пользователем" : "Ошибка выполнения") +
                                    (diagnostic.Line > 0 ? " (строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) + ")" : "") + ": " + diagnostic.Message);
            if (result.StoppedByUser)
            {
                StatusText = "Остановлено пользователем" + (diagnostic.Line > 0 ? " · строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) : "");
            }
            else
            {
                SelectedBottomTab = 1;
                StatusText = "Ошибка выполнения" + (diagnostic.Line > 0 ? " · строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) : "") +
                             ": " + diagnostic.Message;
            }
        }

        private void ApplySnapshot(DebugSnapshot snapshot, ExecutionMarker marker)
        {
            Output = snapshot.Output;
            _frames = snapshot.Frames;
            CallStack = snapshot.CallStack.Select((f, i) => new StackFrameItem(i, f.Function, f.Line)).ToList();

            _previousHeap = _hasHeap ? _heap : null;
            _heap = snapshot.Heap;
            _hasHeap = true;
            IsHeapStale = false;
            RebuildHeapGraph();

            _selectedFrame = CallStack.Count > 0 ? CallStack[0] : null;
            OnPropertyChanged(nameof(SelectedFrame));
            ShowLocals(0);
            if (snapshot.CurrentLine > 0) _editor.ShowExecutionLine(snapshot.CurrentLine, marker);
            else _editor.ShowExecutionLine(null, marker);

            UpdateInspector();
        }

        private void ShowFrame(StackFrameItem frame)
        {
            ShowLocals(frame.Depth);
            if (frame.Line > 0)
            {
                _editor.ShowExecutionLine(frame.Line, frame.Depth == 0 ? ExecutionMarker.Current : ExecutionMarker.Caller);
            }
        }

        private void ShowLocals(int depth)
        {
            FrameLocals? frame = _frames.FirstOrDefault(f => f.Depth == depth);
            Locals = frame == null ? Array.Empty<LocalItem>() : frame.Locals.Select(l => new LocalItem(l)).ToList();
            _selectedLocal = null;
            OnPropertyChanged(nameof(SelectedLocal));
        }

        private void RebuildHeapGraph()
        {
            HeapGraph = HeapGraphBuilder.Build(_heap, _frames, _previousHeap, _selectedAddress);
            OnPropertyChanged(nameof(HeapSummary));
        }

        // ------------------------------------------------------------------
        // Выбор объекта кучи, инспектор
        // ------------------------------------------------------------------

        /// <summary>Выбрать объект по адресу (клик по узлу схемы, по корню или по ссылочной переменной); null — снять выбор.</summary>
        public void SelectHeapNode(string? address)
        {
            _selectedAddress = address;
            HeapGraph.Select(address);
            UpdateInspector();
        }

        private void UpdateInspector()
        {
            if (_selectedAddress == null)
            {
                Inspector.Clear();
                return;
            }
            HeapObject? obj = _heap.Objects.FirstOrDefault(o => o.Address == _selectedAddress);
            if (obj == null)
            {
                _selectedAddress = null;  // объект собран — выбирать больше нечего
                Inspector.Clear();
                return;
            }

            ObjectInfo? info = null;
            if (_session != null && _session.Phase != SessionPhase.Running)
            {
                try { info = _session.InspectObject(obj.Address); } catch (InvalidOperationException) { }
            }
            Inspector.Show(obj, info);
        }

        // ------------------------------------------------------------------
        // Стоп и сборка мусора
        // ------------------------------------------------------------------

        /// <summary>Shift+F5: во время выполнения — прервать; на паузе/после завершения — закрыть сессию.</summary>
        public void Stop()
        {
            if (State == IdeState.Running)
            {
                StatusText = "Останавливаю…";
                _session?.RequestStop();  // команда завершится как «выполнение прервано» → ApplyResult
                return;
            }
            if (State == IdeState.Editing) return;

            _log.Info("Выполнение", "Сессия закрыта (Стоп)");
            EndSession();
            StatusText = "Готово";
        }

        /// <summary>Немедленная сборка мусора (как ~gc). Возвращает число освобождённых объектов, или -1, если сейчас нельзя.</summary>
        public int CollectGarbage()
        {
            if (State != IdeState.Paused || _session == null) return -1;
            int freed = _session.CollectGarbage();
            GcMessage = freed == 0
                ? "Собирать нечего — мусора нет"
                : "Собрано " + freed.ToString(CultureInfo.InvariantCulture) + " " + Plural.Choose(freed, "объект", "объекта", "объектов");

            ApplySnapshot(_session.Capture(MaxFramesWithLocals), ExecutionMarker.Current);
            StatusText = GcMessage;
            _log.Info("Куча", "Сборка мусора по кнопке: освобождено " + freed.ToString(CultureInfo.InvariantCulture));
            return freed;
        }

        private void EndSession()
        {
            DebugSession? old = _session;
            _session = null;
            old?.Dispose();

            _frames = Array.Empty<FrameLocals>();
            CallStack = Array.Empty<StackFrameItem>();
            Locals = Array.Empty<LocalItem>();
            _selectedFrame = null;
            OnPropertyChanged(nameof(SelectedFrame));
            _heap = HeapSnapshot.Empty;
            _previousHeap = null;
            _hasHeap = false;
            _selectedAddress = null;
            HeapGraph = HeapGraphModel.Empty;
            OnPropertyChanged(nameof(HeapSummary));
            IsHeapStale = false;
            Inspector.Clear();
            GcMessage = "";
            IsSourceStale = false;
            _editor.ShowExecutionLine(null, ExecutionMarker.Current);
            State = IdeState.Editing;
        }

        // ------------------------------------------------------------------
        // Файл и примеры
        // ------------------------------------------------------------------

        public void NewFile() => LoadText("", null, null);

        public void OpenExample(ExampleItem example) => LoadText(ExampleCatalog.LoadSource(example.Id), null, example.Title);

        public void OpenFile()
        {
            if (IsModified && !_dialogs.ConfirmDiscardChanges()) return;
            string? path = _dialogs.ChooseFileToOpen();
            if (path == null) return;
            string text;
            try { text = File.ReadAllText(path); }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                StatusText = "Не удалось открыть файл: " + ex.Message;
                return;
            }
            LoadText(text, path, null, alreadyConfirmed: true);
        }

        private void LoadText(string text, string? path, string? exampleTitle, bool alreadyConfirmed = false)
        {
            if (State == IdeState.Running) return;
            if (!alreadyConfirmed && IsModified && !_dialogs.ConfirmDiscardChanges()) return;

            EndSession();
            _breakpoints.Clear();
            _editor.ShowBreakpoints(_breakpoints);
            SetProblems(Array.Empty<Diagnostic>());
            Output = "";

            _suppressTextChanged = true;
            try { _editor.SetText(text); }
            finally { _suppressTextChanged = false; }

            _exampleTitle = exampleTitle;
            CurrentFilePath = path;
            IsModified = false;
            OnPropertyChanged(nameof(WindowTitle));
            StatusText = path != null ? "Открыт файл " + Path.GetFileName(path) : exampleTitle != null ? "Пример: " + exampleTitle : "Готово";
            _log.Info("Файл", path != null ? "Открыт файл " + path : exampleTitle != null ? "Загружено: " + exampleTitle : "Новый файл");
            ScheduleLiveCheck();
        }

        /// <summary>true — сохранено.</summary>
        public bool Save()
        {
            if (CurrentFilePath == null) return SaveAs();
            return WriteTo(CurrentFilePath);
        }

        public bool SaveAs()
        {
            string suggested = CurrentFilePath != null ? Path.GetFileName(CurrentFilePath) : "program.glass";
            string? path = _dialogs.ChooseFileToSave(suggested);
            if (path == null) return false;
            return WriteTo(path);
        }

        private bool WriteTo(string path)
        {
            try
            {
                File.WriteAllText(path, _editor.GetText());
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                StatusText = "Не удалось сохранить: " + ex.Message;
                return false;
            }
            CurrentFilePath = path;
            _exampleTitle = null;
            IsModified = false;
            _log.Info("Файл", "Сохранено: " + path);
            StatusText = "Сохранено: " + Path.GetFileName(path);
            return true;
        }

        // ------------------------------------------------------------------
        // Курс: окно курса читает код из редактора и подставляет в него заготовки уроков
        // ------------------------------------------------------------------

        public string GetSource() => _editor.GetText();

        /// <summary>Подставляет программу в редактор (заготовка урока). false — отказались терять изменения или идёт выполнение.</summary>
        public bool LoadProgram(string text, string title)
        {
            if (State == IdeState.Running) return false;
            if (IsModified && !_dialogs.ConfirmDiscardChanges()) return false;
            LoadText(text, null, title, alreadyConfirmed: true);
            return true;
        }

        /// <summary>Закрытие окна: true — можно закрывать (нет изменений, либо пользователь согласился потерять).</summary>
        public bool ConfirmCanClose() => !IsModified || _dialogs.ConfirmDiscardChanges();

        // ------------------------------------------------------------------
        // Текст изменился: фоновая проверка кода
        // ------------------------------------------------------------------

        /// <summary>Редактор зовёт при каждом изменении текста.</summary>
        public void NotifyTextChanged()
        {
            if (_suppressTextChanged) return;
            IsModified = true;
            if (State == IdeState.Paused || State == IdeState.Finished) IsSourceStale = true;
            ScheduleLiveCheck();
        }

        private void ScheduleLiveCheck()
        {
            if (_liveCheckDelayMs < 0) return;
            var cts = new CancellationTokenSource();
            CancellationTokenSource? previous = Interlocked.Exchange(ref _liveCts, cts);
            previous?.Cancel();
            _ = LiveCheckAfterDelayAsync(cts);
        }

        private async Task LiveCheckAfterDelayAsync(CancellationTokenSource cts)
        {
            try
            {
                await Task.Delay(_liveCheckDelayMs, cts.Token);
                await CheckSourceAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                // пользователь продолжил печатать — проверим позже
            }
            catch (Exception ex)
            {
                StatusText = "Проверка кода не удалась: " + ex.Message;
            }
        }

        /// <summary>Проверить код сейчас (без задержки): ошибки компиляции — в «Ошибки» и подчёркиванием в редакторе.</summary>
        public async Task CheckSourceAsync(CancellationToken cancellation = default)
        {
            string text = _editor.GetText();
            Diagnostic? diagnostic = await Task.Run(() => CompileForDiagnostics(text), cancellation);
            cancellation.ThrowIfCancellationRequested();
            if (State == IdeState.Running) return;  // не затираем результат выполнения
            SetProblems(diagnostic == null ? Array.Empty<Diagnostic>() : new[] { diagnostic });
        }

        private static Diagnostic? CompileForDiagnostics(string source)
        {
            try
            {
                // Отдельная одноразовая сессия: компиляция — чистая функция, с работающей
                // программой пользователя не пересекается (сессии независимы).
                using GlassSession probe = GlassSession.Compile(source);
                return null;
            }
            catch (GlassCompileException ex)
            {
                return DiagnosticParser.ParseCompileError(ex.Message);
            }
        }

        private void SetProblems(IReadOnlyList<Diagnostic> diagnostics)
        {
            Problems = diagnostics.Select(d => new ProblemItem(d)).ToList();
            _editor.ShowProblems(diagnostics);
        }

        // ------------------------------------------------------------------

        private void OnCommandError(Exception ex)
        {
            _log.Error("Команда", "Необработанное исключение в команде", ex);
            StatusText = "Внутренняя ошибка: " + ex.Message;
            if (State == IdeState.Running) EndSession();
        }

        private void RaiseAllCanExecuteChanged()
        {
            foreach (ICommand command in new ICommand[]
            {
                StartOrContinueCommand, RunWithoutDebuggingCommand, StepIntoCommand, StepOverCommand, StepOutCommand,
                StopCommand, CollectGarbageCommand, ClearBreakpointsCommand, NewCommand, OpenCommand, OpenExampleCommand,
            })
            {
                if (command is RelayCommand relay) relay.RaiseCanExecuteChanged();
                else if (command is AsyncRelayCommand asyncRelay) asyncRelay.RaiseCanExecuteChanged();
            }
            foreach (ExampleMenuItem item in ExampleMenu) item.OpenCommand.RaiseCanExecuteChanged();
        }

        public void Dispose()
        {
            _liveCts?.Cancel();
            DebugSession? old = _session;
            _session = null;
            old?.Dispose();
        }
    }
}
