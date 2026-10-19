// P/Invoke-обёртка над native glass_capi (см. ../src/capi/GlassApi.h) —
// то, с чего должна начинаться WPF-часть (см. README «Что дальше»).
//
// Проверено по-настоящему: библиотека собирается (.NET 8, TreatWarningsAs-
// Errors) и гоняется интеграционными тестами (dotnet/Glass.Studio.Tests)
// против живой libglass_capi.so — каждая сигнатура ниже реально вызвана.
// Остаётся одно, чего Linux проверить не может: то же самое на Windows с
// glass_capi.dll (соглашение о вызове там то же — cdecl, строки — UTF-8).
//
// DllImport ищет "glass_capi" — платформенные префикс/расширение (.dll на
// Windows, lib*.so на Linux) .NET подставляет сам. На Windows нативную
// DLL (собранную из glass/CMakeLists.txt, цель glass_capi) нужно положить
// рядом с исполняемым файлом WPF-приложения (или прописать путь поиска).
//
// Кодировка строк — сознательный выбор, не значение по умолчанию:
// native-сторона везде работает с UTF-8 std::string (в проекте всюду
// кириллица — сообщения об ошибках, а потенциально и идентификаторы в
// исходнике Glass), а не ASCII/ANSI. UnmanagedType.LPUTF8Str для строк,
// уходящих В native, и ручной Marshal.PtrToStringUTF8 для строк,
// приходящих ОТТУДА (см. GlassSession.cs) — используя ANSI-маршалинг
// по умолчанию, кириллица бы просто побилась на системах не с UTF-8
// ANSI code page (исторически — почти любая русскоязычная Windows).
//
// CallingConvention.Cdecl — тоже не значение по умолчанию (по умолчанию
// P/Invoke на Windows ожидает stdcall): native-функции — обычные
// extern "C", а у них и в GCC/Clang, и в MSVC по умолчанию cdecl.
// Несовпадение соглашений о вызове — тихая порча стека, а не
// аккуратная ошибка.

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Glass.Interop
{
    /// <summary>
    /// Статусы, которые возвращают glass_run_until_pause/glass_step_* —
    /// см. GLASS_STATUS_* в GlassApi.h.
    /// </summary>
    public enum GlassRunStatus
    {
        Finished = 0,
        PausedAtBreakpoint = 1,
        PausedAfterStep = 2,
    }

    /// <summary>
    /// Сырые P/Invoke-сигнатуры — 1:1 с GlassApi.h. Не предназначены для
    /// использования напрямую из UI-кода — см. GlassSession (в этой же
    /// папке) за удобной, идиоматичной для C# обёрткой (IDisposable,
    /// исключения вместо кодов возврата, автоматический маршалинг строк).
    ///
    /// Параметры session — типа GlassSessionHandle (SafeHandle), не
    /// голый IntPtr: так маршалер сам держит handle живым (AddRef) на
    /// время вызова и отпускает (Release) сразу после — без этого
    /// сборщик мусора теоретически мог бы финализировать (и освободить
    /// нативную сессию) ПРЯМО во время ещё идущего P/Invoke-вызова,
    /// используй мы IntPtr напрямую (DangerousGetHandle() без парного
    /// DangerousAddRef/Release — известная гонка, а не надуманная).
    /// </summary>
    internal static class NativeMethods
    {
        private const string DllName = "glass_capi";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_compile(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string source, out IntPtr errorOut);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void glass_session_free(IntPtr session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void glass_free_string(IntPtr s);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_run(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_get_output(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_has_error(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_get_error(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void glass_set_breakpoint(GlassSessionHandle session, int line);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void glass_clear_breakpoint(GlassSessionHandle session, int line);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void glass_clear_all_breakpoints(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_run_until_pause(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_step_into(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_step_over(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_step_out(GlassSessionHandle session);

        // Единственный вызов, допустимый из другого потока, пока основной
        // поток внутри glass_run/glass_run_until_pause/glass_step_* (см. GlassApi.h).
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void glass_request_stop(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_current_line(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_get_call_stack_json(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_get_locals_json(GlassSessionHandle session, int frameDepth);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_get_heap_json(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int glass_collect_garbage(GlassSessionHandle session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr glass_get_object_info_json(
            GlassSessionHandle session, [MarshalAs(UnmanagedType.LPUTF8Str)] string address);

        /// <summary>
        /// Где искать нативную библиотеку, если стандартный поиск .NET (рядом с
        /// исполняемым файлом / в PATH / LD_LIBRARY_PATH) не подходит:
        /// переменная окружения GLASS_CAPI_PATH — полный путь к glass_capi.dll /
        /// libglass_capi.so (удобно при разработке: не копировать библиотеку
        /// в каждый bin/). Нет переменной или файла — работает стандартный поиск.
        /// </summary>
        static NativeMethods()
        {
            NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, ResolveNativeLibrary);
        }

        private static IntPtr ResolveNativeLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        {
            if (libraryName != DllName) return IntPtr.Zero;
            string? overridePath = Environment.GetEnvironmentVariable("GLASS_CAPI_PATH");
            if (!string.IsNullOrEmpty(overridePath) && File.Exists(overridePath) &&
                NativeLibrary.TryLoad(overridePath, out IntPtr handle))
            {
                return handle;
            }
            return IntPtr.Zero;  // стандартный поиск
        }
    }

    /// <summary>
    /// SafeHandle вокруг GlassSession* — гарантирует glass_session_free
    /// даже если вызывающий забыл вызвать Dispose (через финализацию
    /// SafeHandle — стандартный .NET-паттерн для нативных handle, а не
    /// голый IntPtr россыпью по коду). glass_session_free принимает
    /// голый IntPtr, не SafeHandle, намеренно: ReleaseHandle вызывается
    /// уже тогда, когда SafeHandle решил освободиться, — передавать в
    /// эту функцию сам SafeHandle означало бы держать его живым ради
    /// вызова, который его же и освобождает.
    /// </summary>
    internal sealed class GlassSessionHandle : SafeHandle
    {
        internal GlassSessionHandle(IntPtr handle) : base(IntPtr.Zero, true)
        {
            SetHandle(handle);
        }

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            NativeMethods.glass_session_free(handle);
            return true;
        }
    }

    /// <summary>
    /// Ошибка компиляции исходника Glass (разбор/типы/байткод — см.
    /// glass_compile в GlassApi.h). Не рантайм-ошибка выполненной
    /// программы — за той см. GlassSession.HasError/GetError.
    /// </summary>
    public sealed class GlassCompileException : Exception
    {
        public GlassCompileException(string message) : base(message) { }
    }
}
