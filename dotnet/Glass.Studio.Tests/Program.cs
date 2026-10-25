using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Glass.Studio.Tests
{
    public static class Program
    {
        public static async Task<int> Main(string[] args)
        {
            string? library = FindNativeLibrary();
            if (library == null)
            {
                Console.Error.WriteLine("Не найдена нативная библиотека glass_capi. Соберите C++-часть (cmake --build build)\n" +
                                        "или укажите полный путь в переменной окружения GLASS_CAPI_PATH.");
                return 3;
            }
            Environment.SetEnvironmentVariable("GLASS_CAPI_PATH", library);
            Console.WriteLine("glass_capi: " + library);

            JsonTests.Register();
            HeapGraphTests.Register();
            SessionTests.Register();
            ViewModelTests.Register();
            XamlLintTests.Register();
            LoggingTests.Register();
            CourseTests.Register();
            CodeInfoTests.Register();
            RepoTests.Register();
            ShellTests.Register();
            DocsTests.Register();

            // dotnet run ... -- <часть имени теста> — прогнать только подходящие.
            return await TestKit.RunAllAsync(args.Length > 0 ? args[0] : null);
        }

        private static string? FindNativeLibrary()
        {
            string? fromEnv = Environment.GetEnvironmentVariable("GLASS_CAPI_PATH");
            if (!string.IsNullOrEmpty(fromEnv) && File.Exists(fromEnv)) return fromEnv;

            string name = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "glass_capi.dll"
                        : RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "libglass_capi.dylib"
                        : "libglass_capi.so";
            string[] relatives = { "build", "build/Release", "build/Debug", "out/build/x64-Release", "out/build/x64-Debug", "" };
            for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                foreach (string relative in relatives)
                {
                    string candidate = Path.Combine(dir.FullName, relative, name);
                    if (File.Exists(candidate)) return candidate;
                }
            }
            return null;
        }
    }
}
