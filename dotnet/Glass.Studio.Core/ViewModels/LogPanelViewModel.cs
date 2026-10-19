// Вкладка «Журнал»: живой список записей AppLog с фильтром по уровню. Записи приходят из любых потоков —
// в коллекцию они попадают через контекст синхронизации интерфейса (в WPF — UI-поток; в тестах контекст
// не нужен: добавление идёт сразу).

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Windows.Input;
using Glass.Studio.Logging;
using Glass.Studio.Mvvm;

namespace Glass.Studio.ViewModels
{
    public sealed class LogItem
    {
        public LogItem(LogEntry entry)
        {
            Entry = entry;
        }

        public LogEntry Entry { get; }
        public string TimeText => Entry.TimeText;
        public string LevelText => Entry.LevelText;
        public string Source => Entry.Source;
        public string Message => Entry.Message;
        public bool IsError => Entry.Level == LogLevel.Error;
        public bool IsWarning => Entry.Level == LogLevel.Warning;
        public bool IsDebug => Entry.Level == LogLevel.Debug;

        /// <summary>Сообщение и (если есть) исключение — для подсказки при наведении.</summary>
        public string Details => Entry.Exception == null ? Entry.Message : Entry.Message + Environment.NewLine + Entry.Exception;
    }

    public sealed class LogPanelViewModel : ObservableObject
    {
        private readonly AppLog _log;
        private readonly SynchronizationContext? _context;
        private readonly List<LogEntry> _all = new List<LogEntry>();
        private int _minLevelIndex = 1;   // по умолчанию — от Info: Debug — для разбора полётов

        public LogPanelViewModel(AppLog log, SynchronizationContext? uiContext)
        {
            _log = log ?? throw new ArgumentNullException(nameof(log));
            _context = uiContext;
            Entries = new ObservableCollection<LogItem>();

            _all.AddRange(log.Snapshot());
            Rebuild();
            _log.EntryWritten += OnEntryWritten;

            ClearCommand = new RelayCommand(Clear);
            OpenFolderCommand = new RelayCommand(() => OpenFolder?.Invoke(_log.Directory));
        }

        public ObservableCollection<LogItem> Entries { get; }

        /// <summary>0 — Debug, 1 — Info, 2 — Warning, 3 — Error: показываются записи этого уровня и выше.</summary>
        public int MinLevelIndex
        {
            get => _minLevelIndex;
            set
            {
                int clamped = Math.Max(0, Math.Min(3, value));
                if (!SetProperty(ref _minLevelIndex, clamped)) return;
                Rebuild();
            }
        }

        public string FilePath => _log.CurrentFilePath;

        public string Summary
        {
            get
            {
                string text = "Записей: " + Entries.Count.ToString(CultureInfo.InvariantCulture);
                if (_log.FileError != null) text += " · запись в файл отключена: " + _log.FileError;
                return text;
            }
        }

        public ICommand ClearCommand { get; }
        public ICommand OpenFolderCommand { get; }

        /// <summary>Как открыть папку журналов (в WPF — проводник); null — ничего не делать.</summary>
        public Action<string>? OpenFolder { get; set; }

        public void Clear()
        {
            _all.Clear();
            Rebuild();
        }

        private void OnEntryWritten(LogEntry entry)
        {
            if (_context == null) Add(entry);
            else _context.Post(_ => Add(entry), null);
        }

        private void Add(LogEntry entry)
        {
            _all.Add(entry);
            if (_all.Count > 1000) _all.RemoveAt(0);
            if ((int)entry.Level >= _minLevelIndex)
            {
                Entries.Add(new LogItem(entry));
                while (Entries.Count > 1000) Entries.RemoveAt(0);
            }
            OnPropertyChanged(nameof(Summary));
        }

        private void Rebuild()
        {
            Entries.Clear();
            foreach (LogEntry entry in _all.Where(e => (int)e.Level >= _minLevelIndex)) Entries.Add(new LogItem(entry));
            OnPropertyChanged(nameof(Summary));
        }
    }
}
