using Glass.Studio.CodeInfo;
using Glass.Studio.Mvvm;

namespace Glass.Studio.ViewModels
{
    /// <summary>Карточка элемента кода (всплывающее окно по правой кнопке мыши в редакторе).</summary>
    public sealed class CodeInfoCardViewModel : ObservableObject
    {
        private ElementInfo? _info;
        private bool _isOpen;

        /// <summary>Карточка показана. Окно привязывает к этому свойству Popup.IsOpen (TwoWay): клик мимо закрывает его само.</summary>
        public bool IsOpen
        {
            get => _isOpen;
            set => SetProperty(ref _isOpen, value);
        }

        public ElementInfo? Info
        {
            get => _info;
            private set
            {
                if (!SetProperty(ref _info, value)) return;
                OnPropertyChanged(nameof(HasInfo));
                OnPropertyChanged(nameof(HasNoInfo));
                OnPropertyChanged(nameof(Title));
                OnPropertyChanged(nameof(KindText));
                OnPropertyChanged(nameof(Description));
                OnPropertyChanged(nameof(Example));
                OnPropertyChanged(nameof(HasExample));
                OnPropertyChanged(nameof(Origin));
                OnPropertyChanged(nameof(HasOrigin));
                OnPropertyChanged(nameof(DocUrl));
                OnPropertyChanged(nameof(HasDoc));
            }
        }

        public bool HasInfo => _info != null;

        /// <summary>Подсказки для этого места нет — в карточке остаются только действия редактора.</summary>
        public bool HasNoInfo => _info == null;
        public string Title => _info?.Title ?? "";
        public string KindText => _info?.KindText ?? "";
        public string Description => _info?.Description ?? "";
        public string Example => _info?.Example ?? "";
        public bool HasExample => !string.IsNullOrEmpty(_info?.Example);
        public string Origin => _info?.Origin ?? "";
        public bool HasOrigin => !string.IsNullOrEmpty(_info?.Origin);
        public string? DocUrl => _info?.DocUrl;
        public bool HasDoc => _info?.DocUrl != null;

        /// <summary>Показать карточку для элемента в позиции offset текста; false — для этого места подсказки нет.</summary>
        public bool Show(string source, int offset)
        {
            Info = CodeInfoProvider.Describe(source, offset);
            return Info != null;
        }

        /// <summary>
        /// Открыть карточку для места offset. Открывается всегда: если для этого места подсказки нет (пробел, скобка),
        /// в ней остаются только действия редактора — правая кнопка мыши не должна «молчать». true — подсказка нашлась.
        /// </summary>
        public bool Open(string source, int offset)
        {
            bool found = Show(source, offset);
            IsOpen = true;
            return found;
        }

        /// <summary>Закрыть карточку (подсказка забывается, чтобы при следующем показе не мелькнуло старое).</summary>
        public void Close()
        {
            IsOpen = false;
            Info = null;
        }

        public void Clear() => Info = null;
    }
}
