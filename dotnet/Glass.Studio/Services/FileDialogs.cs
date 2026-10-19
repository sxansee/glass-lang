using System.Windows;
using Glass.Studio.ViewModels;
using Microsoft.Win32;

namespace Glass.Studio.Services
{
    /// <summary>Стандартные диалоги Windows для ViewModel (она о них ничего не знает — см. IFileDialogs).</summary>
    public sealed class FileDialogs : IFileDialogs
    {
        private const string Filter = "Программы Glass (*.glass)|*.glass|Все файлы (*.*)|*.*";
        private readonly Window _owner;

        public FileDialogs(Window owner)
        {
            _owner = owner;
        }

        public string? ChooseFileToOpen()
        {
            var dialog = new OpenFileDialog { Filter = Filter, Title = "Открыть программу Glass" };
            return dialog.ShowDialog(_owner) == true ? dialog.FileName : null;
        }

        public string? ChooseFileToSave(string suggestedName)
        {
            var dialog = new SaveFileDialog
            {
                Filter = Filter,
                Title = "Сохранить программу Glass",
                FileName = suggestedName,
                DefaultExt = ".glass",
                AddExtension = true,
            };
            return dialog.ShowDialog(_owner) == true ? dialog.FileName : null;
        }

        public bool ConfirmDiscardChanges()
        {
            MessageBoxResult answer = MessageBox.Show(_owner,
                "В программе есть несохранённые изменения. Продолжить без сохранения?",
                "Glass Studio", MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No);
            return answer == MessageBoxResult.Yes;
        }
    }
}
