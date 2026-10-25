using System;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;
using Glass.Interop;
using Glass.Studio.Logging;
using Glass.Studio.Services;

namespace Glass.Studio
{
    public partial class App : Application
    {
        public App()
        {
            // Три места, где исключение может проскочить мимо try/catch: поток интерфейса, любой другой поток, фоновая задача.
            // Во всех трёх сначала записываем причину в журнал — по нему потом можно разобраться, что случилось.
            DispatcherUnhandledException += OnDispatcherUnhandledException;
            AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;
            TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
        }

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            AppServices.Log.Info("Приложение", AppInfo.Name + " " + AppInfo.Version + " запущен");

            // Самая частая беда первого запуска — нет нативной библиотеки или не та разрядность.
            // Объясняем это сразу и по-человечески, а не «Unable to load DLL» посреди работы.
            string? problem = ProbeNativeLibrary();
            if (problem != null)
            {
                AppServices.Log.Warning("Приложение", problem.Replace("\n", " "));
                MessageBox.Show(problem, "Glass Studio", MessageBoxButton.OK, MessageBoxImage.Warning);
            }

            new MainWindow().Show();
        }

        protected override void OnExit(ExitEventArgs e)
        {
            AppServices.Log.Info("Приложение", "Закрыто, код выхода " + e.ApplicationExitCode);
            base.OnExit(e);
        }

        private static string? ProbeNativeLibrary()
        {
            try
            {
                using GlassSession probe = GlassSession.Compile("fn main() {\n}\n");
                return null;
            }
            catch (DllNotFoundException)
            {
                return "Не найдена нативная библиотека glass_capi.dll — она должна лежать рядом с GlassStudio.exe.\n\n" +
                       "Соберите C++-часть проекта (см. README, «Сборка на Windows») и пересоберите Glass Studio: " +
                       "проект сам копирует библиотеку из build\\<Config> или out\\build\\x64-<Config>.\n\n" +
                       "Окно откроется, но запускать программы не получится.";
            }
            catch (BadImageFormatException)
            {
                return "Библиотека glass_capi.dll собрана под другую разрядность, чем GlassStudio.exe (он 64-битный).\n\n" +
                       "Соберите C++-часть как x64.";
            }
            catch (Exception ex)
            {
                return "Нативная библиотека загрузилась, но проверочная компиляция не удалась:\n" + ex.Message;
            }
        }

        private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            AppServices.Log.Error("Приложение", "Необработанное исключение в потоке интерфейса", e.Exception);
            MessageBox.Show("Неожиданная ошибка: " + e.Exception.Message +
                            "\n\nПодробности записаны в журнал (вкладка «Журнал» или Справка → Открыть папку журналов).",
                "Glass Studio", MessageBoxButton.OK, MessageBoxImage.Error);
            e.Handled = true;   // IDE не должна закрываться из-за одного сбоя в обработчике
        }

        private static void OnDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
        {
            // Процесс уже завершается — главное успеть записать причину.
            AppServices.Log.Error("Приложение", "Необработанное исключение (процесс завершается: " + e.IsTerminating + ")", e.ExceptionObject as Exception);
        }

        private static void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
        {
            AppServices.Log.Error("Приложение", "Необработанное исключение в фоновой задаче", e.Exception);
            e.SetObserved();
        }
    }
}
