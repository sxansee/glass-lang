using System;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Glass.Studio.Mvvm
{
    /// <summary>
    /// Обычная команда. WPF-овский CommandManager.RequerySuggested здесь недоступен
    /// (это переносимая библиотека), поэтому CanExecute пересчитывается явно:
    /// ViewModel зовёт RaiseCanExecuteChanged() при смене состояния.
    /// </summary>
    public sealed class RelayCommand : ICommand
    {
        private readonly Action<object?> _execute;
        private readonly Func<object?, bool>? _canExecute;

        public RelayCommand(Action execute, Func<bool>? canExecute = null)
            : this(_ => execute(), canExecute == null ? null : new Func<object?, bool>(_ => canExecute()))
        {
        }

        public RelayCommand(Action<object?> execute, Func<object?, bool>? canExecute = null)
        {
            _execute = execute ?? throw new ArgumentNullException(nameof(execute));
            _canExecute = canExecute;
        }

        public event EventHandler? CanExecuteChanged;

        public bool CanExecute(object? parameter) => _canExecute?.Invoke(parameter) ?? true;

        public void Execute(object? parameter)
        {
            if (CanExecute(parameter)) _execute(parameter);
        }

        public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>
    /// Асинхронная команда: пока предыдущий запуск не завершён, повторно не стартует
    /// (двойной F5 не должен запустить программу дважды). Исключение внутри не
    /// роняет приложение — уходит в onError.
    /// </summary>
    public sealed class AsyncRelayCommand : ICommand
    {
        private readonly Func<object?, Task> _execute;
        private readonly Func<object?, bool>? _canExecute;
        private readonly Action<Exception> _onError;
        private bool _isExecuting;

        public AsyncRelayCommand(Func<Task> execute, Func<bool>? canExecute, Action<Exception> onError)
            : this(_ => execute(), canExecute == null ? null : new Func<object?, bool>(_ => canExecute()), onError)
        {
        }

        public AsyncRelayCommand(Func<object?, Task> execute, Func<object?, bool>? canExecute, Action<Exception> onError)
        {
            _execute = execute ?? throw new ArgumentNullException(nameof(execute));
            _canExecute = canExecute;
            _onError = onError ?? throw new ArgumentNullException(nameof(onError));
        }

        public event EventHandler? CanExecuteChanged;

        public bool CanExecute(object? parameter) => !_isExecuting && (_canExecute?.Invoke(parameter) ?? true);

        // async void — единственный способ реализовать ICommand.Execute асинхронно;
        // безопасно, потому что всё тело под try/catch.
        public async void Execute(object? parameter)
        {
            if (!CanExecute(parameter)) return;
            _isExecuting = true;
            RaiseCanExecuteChanged();
            try
            {
                await _execute(parameter);
            }
            catch (Exception ex)
            {
                _onError(ex);
            }
            finally
            {
                _isExecuting = false;
                RaiseCanExecuteChanged();
            }
        }

        public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
    }
}
