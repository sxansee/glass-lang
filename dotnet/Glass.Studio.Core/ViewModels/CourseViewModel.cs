// Окно курса: список глав и уроков со звёздами, теория, задача, проверка решения прямо из редактора IDE.
// Вся логика (выбор урока, подсказки, звёзды, прогресс, проверка) — здесь, в переносимом ядре, и проверяется
// тестами с настоящей VM; WPF-окно только показывает.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using Glass.Studio.Course;
using Glass.Studio.Logging;
using Glass.Studio.Mvvm;

namespace Glass.Studio.ViewModels
{
    public sealed class LessonItem : ObservableObject
    {
        private bool _isCompleted;
        private bool _isSelected;
        private string _starsText = "";

        public LessonItem(Lesson lesson)
        {
            Lesson = lesson;
        }

        public Lesson Lesson { get; }
        public string Title => Lesson.Title;
        public string NumberText => Lesson.NumberText;
        public string Summary => Lesson.Summary;

        public bool IsCompleted { get => _isCompleted; private set => SetProperty(ref _isCompleted, value); }
        public bool IsSelected { get => _isSelected; internal set => SetProperty(ref _isSelected, value); }

        /// <summary>«★★☆» у пройденного урока; пусто у непройденного.</summary>
        public string StarsText { get => _starsText; private set => SetProperty(ref _starsText, value); }

        internal void Refresh(CourseProgress progress)
        {
            bool done = progress.IsCompleted(Lesson.Id);
            IsCompleted = done;
            StarsText = done ? CourseScoring.StarsText(progress.Record(Lesson.Id).Stars) : "";
        }
    }

    public sealed class ChapterItem : ObservableObject
    {
        private string _progressText = "";

        public ChapterItem(Chapter chapter, IReadOnlyList<LessonItem> lessons)
        {
            Chapter = chapter;
            Lessons = lessons;
        }

        public Chapter Chapter { get; }
        public string HeaderText => "Глава " + Chapter.Number.ToString(CultureInfo.InvariantCulture) + ". " + Chapter.Title;
        public string Subtitle => Chapter.Subtitle;
        public IReadOnlyList<LessonItem> Lessons { get; }
        public string ProgressText { get => _progressText; private set => SetProperty(ref _progressText, value); }

        internal void Refresh()
        {
            ProgressText = Lessons.Count(l => l.IsCompleted).ToString(CultureInfo.InvariantCulture) + " из " +
                           Lessons.Count.ToString(CultureInfo.InvariantCulture);
        }
    }

    public enum ResultKind { None, Success, Failure }

    public sealed class CourseViewModel : ObservableObject
    {
        private readonly CourseProgressStore _store;
        private readonly ICourseHost _host;
        private readonly ILog _log;
        private readonly CourseProgress _progress;
        private readonly TimeSpan? _checkTimeout;

        private LessonItem? _selected;
        private IReadOnlyList<ContentBlock> _theory = Array.Empty<ContentBlock>();
        private RichText _taskText = new RichText(Array.Empty<InlineSpan>());
        private string _lessonSubtitle = "";
        private bool _isHintVisible;
        private bool _isSolutionVisible;
        private ResultKind _result = ResultKind.None;
        private string _resultTitle = "";
        private string _resultMessage = "";
        private string _actualOutput = "";
        private string _expectedOutput = "";
        private bool _isChecking;
        private string _statusText = "";

        public CourseViewModel(CourseProgressStore store, ICourseHost host, ILog? log = null, TimeSpan? checkTimeout = null)
        {
            _store = store ?? throw new ArgumentNullException(nameof(store));
            _host = host ?? throw new ArgumentNullException(nameof(host));
            _log = log ?? NullLog.Instance;
            _checkTimeout = checkTimeout;
            _progress = _store.Load();

            Chapters = CourseCatalog.Chapters
                .Select(c => new ChapterItem(c, c.Lessons.Select(l => new LessonItem(l)).ToList()))
                .ToList();
            AllLessons = Chapters.SelectMany(c => c.Lessons).ToList();

            StartCommand = new RelayCommand(Start);
            ShowWelcomeCommand = new RelayCommand(() => SelectedLesson = null);
            SelectLessonCommand = new RelayCommand(p => { if (p is LessonItem item) SelectedLesson = item; });
            LoadStarterCommand = new RelayCommand(LoadStarter, () => HasLesson);
            LoadSolutionCommand = new RelayCommand(LoadSolution, () => HasLesson && IsSolutionVisible);
            CheckCommand = new AsyncRelayCommand(CheckAsync, () => HasLesson && !IsChecking, OnCommandError);
            HintCommand = new RelayCommand(ShowHint, () => HasLesson && !IsHintVisible);
            SolutionCommand = new RelayCommand(ShowSolution, () => HasLesson && !IsSolutionVisible);
            NextCommand = new RelayCommand(Next, () => CanGoNext);
            PreviousCommand = new RelayCommand(Previous, () => CanGoPrevious);
            ResetProgressCommand = new RelayCommand(ResetProgress);

            RefreshAll();
        }

        public IReadOnlyList<ChapterItem> Chapters { get; }
        public IReadOnlyList<LessonItem> AllLessons { get; }

        public LessonItem? SelectedLesson
        {
            get => _selected;
            set
            {
                if (ReferenceEquals(_selected, value)) return;
                if (_selected != null) _selected.IsSelected = false;
                _selected = value;
                if (_selected != null) _selected.IsSelected = true;
                OnPropertyChanged();
                LoadSelectedLesson();
            }
        }

        public bool HasLesson => _selected != null;
        public bool ShowWelcome => _selected == null;

        public string LessonTitle => _selected == null ? "" : _selected.NumberText + "  " + _selected.Title;
        public string LessonSubtitle { get => _lessonSubtitle; private set => SetProperty(ref _lessonSubtitle, value); }
        public IReadOnlyList<ContentBlock> Theory { get => _theory; private set => SetProperty(ref _theory, value); }
        public RichText TaskText { get => _taskText; private set => SetProperty(ref _taskText, value); }

        public bool IsHintVisible { get => _isHintVisible; private set { if (SetProperty(ref _isHintVisible, value)) RaiseCanExecute(); } }
        public string HintText => _selected == null ? "" : _selected.Lesson.Hint;
        public bool IsSolutionVisible { get => _isSolutionVisible; private set { if (SetProperty(ref _isSolutionVisible, value)) RaiseCanExecute(); } }
        public string SolutionText => _selected == null ? "" : _selected.Lesson.Solution;

        public ResultKind Result { get => _result; private set { if (SetProperty(ref _result, value)) { OnPropertyChanged(nameof(HasResult)); OnPropertyChanged(nameof(IsSuccess)); OnPropertyChanged(nameof(IsFailure)); } } }
        public bool HasResult => Result != ResultKind.None;
        public bool IsSuccess => Result == ResultKind.Success;
        public bool IsFailure => Result == ResultKind.Failure;
        public string ResultTitle { get => _resultTitle; private set => SetProperty(ref _resultTitle, value); }
        public string ResultMessage { get => _resultMessage; private set => SetProperty(ref _resultMessage, value); }

        /// <summary>Что напечатала программа ученика.</summary>
        public string ActualOutput { get => _actualOutput; private set => SetProperty(ref _actualOutput, value); }

        /// <summary>Что должно быть напечатано (для строк-шаблонов «re:» — описание шаблона).</summary>
        public string ExpectedOutput { get => _expectedOutput; private set => SetProperty(ref _expectedOutput, value); }
        public bool ShowOutputComparison => IsFailure && (ActualOutput.Length > 0 || ExpectedOutput.Length > 0);

        public bool IsChecking { get => _isChecking; private set { if (SetProperty(ref _isChecking, value)) RaiseCanExecute(); } }
        public string StatusText { get => _statusText; private set => SetProperty(ref _statusText, value); }

        // ---- прогресс
        public int CompletedCount => _progress.CompletedCount;
        public int TotalCount => AllLessons.Count;
        public string ProgressText => "Пройдено " + CompletedCount.ToString(CultureInfo.InvariantCulture) + " из " + TotalCount.ToString(CultureInfo.InvariantCulture);
        public double ProgressPercent => TotalCount == 0 ? 0 : 100.0 * CompletedCount / TotalCount;
        public string LevelText => "Уровень: " + CourseScoring.LevelTitle(CompletedCount, TotalCount);
        public string StarsSummary => "★ " + _progress.TotalStars.ToString(CultureInfo.InvariantCulture) + " из " + (TotalCount * 3).ToString(CultureInfo.InvariantCulture);
        public bool IsAllDone => TotalCount > 0 && CompletedCount == TotalCount;

        public int SelectedIndex => _selected == null ? -1 : AllLessons.ToList().IndexOf(_selected);
        public bool CanGoNext => SelectedIndex >= 0 && SelectedIndex < AllLessons.Count - 1;
        public bool CanGoPrevious => SelectedIndex > 0;

        public ICommand StartCommand { get; }
        public ICommand ShowWelcomeCommand { get; }
        public ICommand SelectLessonCommand { get; }
        public ICommand LoadStarterCommand { get; }
        public ICommand LoadSolutionCommand { get; }
        public ICommand CheckCommand { get; }
        public ICommand HintCommand { get; }
        public ICommand SolutionCommand { get; }
        public ICommand NextCommand { get; }
        public ICommand PreviousCommand { get; }
        public ICommand ResetProgressCommand { get; }

        // ------------------------------------------------------------------

        /// <summary>«Начать курс»: первый непройденный урок (или последний открытый, если он ещё не пройден).</summary>
        public void Start()
        {
            LessonItem? target = AllLessons.FirstOrDefault(l => l.Lesson.Id == _progress.LastLessonId && !l.IsCompleted)
                                 ?? AllLessons.FirstOrDefault(l => !l.IsCompleted)
                                 ?? AllLessons[0];
            SelectedLesson = target;
        }

        private void LoadSelectedLesson()
        {
            ClearResult();
            IsHintVisible = false;
            IsSolutionVisible = false;
            StatusText = "";

            if (_selected == null)
            {
                Theory = Array.Empty<ContentBlock>();
                TaskText = new RichText(Array.Empty<InlineSpan>());
                LessonSubtitle = "";
            }
            else
            {
                Lesson lesson = _selected.Lesson;
                Chapter chapter = CourseCatalog.Chapters.First(c => c.Number == lesson.ChapterNumber);
                Theory = LessonMarkup.Parse(lesson.Theory);
                TaskText = RichText.Parse(lesson.Task);
                LessonSubtitle = "Глава " + chapter.Number.ToString(CultureInfo.InvariantCulture) + " · " + chapter.Title;
                _progress.LastLessonId = lesson.Id;
                _store.Save(_progress);
                _log.Info("Курс", "Открыт урок " + lesson.NumberText + " «" + lesson.Title + "»");
            }

            OnPropertyChanged(nameof(HasLesson));
            OnPropertyChanged(nameof(ShowWelcome));
            OnPropertyChanged(nameof(LessonTitle));
            OnPropertyChanged(nameof(HintText));
            OnPropertyChanged(nameof(SolutionText));
            OnPropertyChanged(nameof(SelectedIndex));
            OnPropertyChanged(nameof(CanGoNext));
            OnPropertyChanged(nameof(CanGoPrevious));
            RaiseCanExecute();
        }

        public void LoadStarter()
        {
            if (_selected == null) return;
            Lesson lesson = _selected.Lesson;
            bool loaded = _host.LoadProgram(lesson.Starter, "Курс: " + lesson.NumberText + " " + lesson.Title);
            StatusText = loaded ? "Заготовка загружена в редактор — напишите решение и нажмите «Проверить»." : "Заготовка не загружена: в редакторе остались ваши изменения.";
        }

        public void LoadSolution()
        {
            if (_selected == null || !IsSolutionVisible) return;
            Lesson lesson = _selected.Lesson;
            bool loaded = _host.LoadProgram(lesson.Solution, "Курс: " + lesson.NumberText + " " + lesson.Title + " (решение)");
            StatusText = loaded ? "Решение загружено в редактор — запустите его (F5) и разберитесь, как оно работает." : "Решение не загружено: в редакторе остались ваши изменения.";
        }

        public void ShowHint()
        {
            if (_selected == null) return;
            _progress.Record(_selected.Lesson.Id).HintUsed = true;
            _store.Save(_progress);
            IsHintVisible = true;
            _log.Info("Курс", "Подсказка к уроку " + _selected.Lesson.NumberText);
        }

        public void ShowSolution()
        {
            if (_selected == null) return;
            _progress.Record(_selected.Lesson.Id).SolutionShown = true;
            _store.Save(_progress);
            IsSolutionVisible = true;
            _log.Info("Курс", "Показано решение урока " + _selected.Lesson.NumberText);
        }

        public async Task CheckAsync()
        {
            if (_selected == null || IsChecking) return;
            LessonItem item = _selected;
            Lesson lesson = item.Lesson;
            IsChecking = true;
            ClearResult();
            StatusText = "Проверяю…";
            try
            {
                CheckResult result = await LessonChecker.CheckAsync(lesson, _host.GetSource(), _checkTimeout);
                if (!ReferenceEquals(_selected, item)) return;   // за время проверки перешли на другой урок

                LessonRecord record = _progress.Record(lesson.Id);
                record.Attempts++;
                if (result.Passed)
                {
                    int stars = CourseScoring.Stars(record.Attempts, record.HintUsed, record.SolutionShown);
                    bool firstTime = !record.Completed;
                    record.Completed = true;
                    record.Stars = Math.Max(record.Stars, stars);
                    if (firstTime) record.CompletedUtc = DateTime.UtcNow;
                    ResultTitle = (firstTime ? "Урок пройден! " : "Верно! ") + CourseScoring.StarsText(stars);
                    ResultMessage = firstTime
                        ? "+" + (stars * CourseScoring.PointsPerStar).ToString(CultureInfo.InvariantCulture) + " очков. " +
                          (CanGoNext ? "Переходите к следующему уроку." : "Это был последний урок — курс пройден!")
                        : "Решение снова верное.";
                    Result = ResultKind.Success;
                    _log.Info("Курс", "Урок " + lesson.NumberText + " пройден, звёзд: " + stars.ToString(CultureInfo.InvariantCulture) +
                                      ", попыток: " + record.Attempts.ToString(CultureInfo.InvariantCulture));
                }
                else
                {
                    ResultTitle = result.Status switch
                    {
                        CheckStatus.CompileError => "Ошибка в коде",
                        CheckStatus.RuntimeError => "Программа упала",
                        CheckStatus.Timeout => "Слишком долго",
                        CheckStatus.MissingFragment => "Почти!",
                        _ => "Пока не совпадает",
                    };
                    ResultMessage = result.Message;
                    ActualOutput = string.Join("\n", result.ActualOutput);
                    ExpectedOutput = string.Join("\n", result.ExpectedOutput.Select(DescribeExpected));
                    Result = ResultKind.Failure;
                    _log.Info("Курс", "Урок " + lesson.NumberText + ": проверка не пройдена (" + result.Status + "): " + result.Message);
                }
                _store.Save(_progress);
                StatusText = "";
                RefreshAll();
            }
            finally
            {
                IsChecking = false;
                OnPropertyChanged(nameof(ShowOutputComparison));
            }
        }

        private static string DescribeExpected(string line) => line.StartsWith("re:", StringComparison.Ordinal) ? "(строка вида: " + line.Substring(3) + ")" : line;

        public void Next()
        {
            if (CanGoNext) SelectedLesson = AllLessons[SelectedIndex + 1];
        }

        public void Previous()
        {
            if (CanGoPrevious) SelectedLesson = AllLessons[SelectedIndex - 1];
        }

        public void ResetProgress()
        {
            _progress.Lessons.Clear();
            _progress.LastLessonId = null;
            _store.Save(_progress);
            _log.Info("Курс", "Прогресс сброшен");
            ClearResult();
            IsHintVisible = false;
            IsSolutionVisible = false;
            RefreshAll();
            SelectedLesson = null;
        }

        private void ClearResult()
        {
            Result = ResultKind.None;
            ResultTitle = "";
            ResultMessage = "";
            ActualOutput = "";
            ExpectedOutput = "";
            OnPropertyChanged(nameof(ShowOutputComparison));
        }

        private void RefreshAll()
        {
            foreach (LessonItem lesson in AllLessons) lesson.Refresh(_progress);
            foreach (ChapterItem chapter in Chapters) chapter.Refresh();
            OnPropertyChanged(nameof(CompletedCount));
            OnPropertyChanged(nameof(ProgressText));
            OnPropertyChanged(nameof(ProgressPercent));
            OnPropertyChanged(nameof(LevelText));
            OnPropertyChanged(nameof(StarsSummary));
            OnPropertyChanged(nameof(IsAllDone));
            OnPropertyChanged(nameof(CanGoNext));
        }

        private void OnCommandError(Exception ex)
        {
            _log.Error("Курс", "Ошибка в команде курса", ex);
            StatusText = "Внутренняя ошибка: " + ex.Message;
            IsChecking = false;
        }

        private void RaiseCanExecute()
        {
            foreach (ICommand command in new ICommand[] { LoadStarterCommand, LoadSolutionCommand, HintCommand, SolutionCommand, NextCommand, PreviousCommand })
            {
                if (command is RelayCommand relay) relay.RaiseCanExecuteChanged();
            }
            if (CheckCommand is AsyncRelayCommand asyncRelay) asyncRelay.RaiseCanExecuteChanged();
        }
    }
}
