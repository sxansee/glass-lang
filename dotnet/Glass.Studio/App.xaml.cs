using System;
using System.Windows;
using System.Windows.Threading;
using Glass.Interop;

namespace Glass.Studio
{
    public partial class App : Application
    {
        public App()
        {
            DispatcherUnhandledException += OnDispatcherUnhandledException;
        }

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            // Самая частая беда первого запуска — нет нативной библиотеки или не та разрядность.
            // Объясняем это сразу и по-человечески, а не «Unable to load DLL» посреди работы.
            string? problem = ProbeNativeLibrary();
            if (problem != null)
            {
                MessageBox.Show(problem, "Glass Studio", MessageBoxButton.OK, MessageBoxImage.Warning);
            }

            new MainWindow().Show();
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
            MessageBox.Show("Неожиданная ошибка: " + e.Exception.Message, "Glass Studio",
                MessageBoxButton.OK, MessageBoxImage.Error);
            e.Handled = true;   // IDE не должна закрываться из-за одного сбоя в обработчике
        }
    }
}
