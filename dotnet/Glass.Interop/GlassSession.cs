// Идиоматичная C#-обёртка над NativeMethods (см. соседний файл) — то,
// чем должен пользоваться WPF-код, а не сырыми P/Invoke-сигнатурами
// напрямую. Собирается и проверяется интеграционными тестами против
// живой libglass_capi.so (dotnet/Glass.Studio.Tests).

using System;
using System.Runtime.InteropServices;

namespace Glass.Interop
{
    /// <summary>
    /// Одна сессия glass_capi — одна скомпилированная программа Glass +
    /// одна живая VM с её кучей (см. vm/VM.h). Использовать так же, как
    /// C++-тесты (tests/*_tests.cpp) уже используют VM напрямую:
    /// Compile() один раз, дальше — либо Run() целиком, либо пошаговая
    /// отладка (SetBreakpoint/RunUntilPause/Step*). Смешивать технически
    /// безопасно (VM это гарантирует и тестами покрыто — после начатой
    /// пошаговой отладки Run() доработает с текущего места, а не
    /// начнёт заново).
    /// </summary>
    public sealed class GlassSession : IDisposable
    {
        private readonly GlassSessionHandle _handle;
        private bool _disposed;

        private GlassSession(GlassSessionHandle handle)
        {
            _handle = handle;
        }

        /// <summary>
        /// Компилирует исходник Glass (тот же пайплайн, что и glassc —
        /// включая пролог с Exception/LayoutInfo/VTableInfo/HeapSnapshot,
        /// см. prelude/Prelude.h). Бросает GlassCompileException с
        /// диагностикой первой найденной ошибки (разбор/типы/байткод),
        /// если компиляция не удалась.
        /// </summary>
        public static GlassSession Compile(string source)
        {
            IntPtr sessionPtr = NativeMethods.glass_compile(source, out IntPtr errorPtr);
            if (sessionPtr == IntPtr.Zero)
            {
                string message = ConsumeUtf8String(errorPtr) ?? "неизвестная ошибка компиляции";
                throw new GlassCompileException(message);
            }
            if (errorPtr != IntPtr.Zero)
            {
                // Не ожидается при успехе (см. GlassApi.cpp: error_out
                // остаётся NULL, если сессия создана) — но если
                // native-сторона когда-нибудь разойдётся с этим
                // контрактом, лучше не течь, чем молча тянуть утечку.
                NativeMethods.glass_free_string(errorPtr);
            }
            return new GlassSession(new GlassSessionHandle(sessionPtr));
        }

        /// <summary>Выполняет main() от начала до конца (или до ошибки/исключения). true — без ошибок.</summary>
        public bool Run() => NativeMethods.glass_run(_handle) == 0;

        /// <summary>
        /// Весь вывод print() с начала выполнения — одна строка на
        /// вызов print(), соединены через '\n' (см. GlassApi.h). Можно
        /// звать в любой момент, в том числе во время пошаговой отладки.
        /// Отдаётся одной строкой, не массивом: если сама печатаемая
        /// строка Glass-программы содержит перевод строки, отличить его
        /// от границы между двумя print() после склейки уже нельзя —
        /// для отображения в UI это обычно и не нужно, а раздельный
        /// список вызовов print() эта функция не даёт в принципе.
        /// </summary>
        public string GetOutput() => ConsumeUtf8String(NativeMethods.glass_get_output(_handle)) ?? string.Empty;

        /// <summary>Была ли ошибка выполнения (компиляция — см. Compile(), которая бросает исключение сама).</summary>
        public bool HasError => NativeMethods.glass_has_error(_handle) != 0;

        /// <summary>Текст последней ошибки выполнения (рантайм/необработанное исключение), или null, если ошибки не было.</summary>
        public string? GetError() => ConsumeUtf8String(NativeMethods.glass_get_error(_handle));

        public void SetBreakpoint(int line) => NativeMethods.glass_set_breakpoint(_handle, line);
        public void ClearBreakpoint(int line) => NativeMethods.glass_clear_breakpoint(_handle, line);
        public void ClearAllBreakpoints() => NativeMethods.glass_clear_all_breakpoints(_handle);

        /// <summary>Запускает (при первом вызове) или продолжает до ближайшей точки останова, конца программы или ошибки.</summary>
        public GlassRunStatus RunUntilPause() => (GlassRunStatus)NativeMethods.glass_run_until_pause(_handle);
        /// <summary>Шаг с заходом внутрь вызванной функции, если текущая строка её вызывает.</summary>
        public GlassRunStatus StepInto() => (GlassRunStatus)NativeMethods.glass_step_into(_handle);
        /// <summary>Шаг без остановки внутри вызванной функции — она выполняется целиком.</summary>
        public GlassRunStatus StepOver() => (GlassRunStatus)NativeMethods.glass_step_over(_handle);
        /// <summary>Доработать текущую функцию до конца, остановиться у вызывающего.</summary>
        public GlassRunStatus StepOut() => (GlassRunStatus)NativeMethods.glass_step_out(_handle);

        /// <summary>
        /// Просит остановить выполнение. ЕДИНСТВЕННЫЙ метод сессии, который
        /// можно звать из другого потока, пока основной поток внутри Run()/
        /// RunUntilPause()/Step*() — иначе из зависшей программы (while true {})
        /// нельзя было бы выйти. Вызов, в котором застрял основной поток,
        /// вернётся как при рантайм-ошибке (GetError() — "выполнение прервано").
        /// </summary>
        public void RequestStop() => NativeMethods.glass_request_stop(_handle);

        /// <summary>Текущая исходная строка, или -1, если выполнение не начато или уже завершилось.</summary>
        public int CurrentLine => NativeMethods.glass_current_line(_handle);

        /// <summary>
        /// [{"function": "имя", "line": N}, ...] от текущего (самого
        /// глубокого) фрейма к main() (см. GlassApi.h). Сырой JSON, не
        /// распарсенный — конкретное отображение решает UI-слой,
        /// System.Text.Json (в составе .NET, отдельная зависимость не
        /// нужна) справится с этой формой без дополнительных DTO.
        /// </summary>
        public string GetCallStackJson() => ConsumeUtf8String(NativeMethods.glass_get_call_stack_json(_handle)) ?? "[]";

        /// <summary>
        /// [{"name": "x", "value": ...}, ...] для фрейма на глубине
        /// frameDepth (0 — текущий, самый глубокий; см.
        /// GetCallStackJson() за списком доступных глубин). Форма
        /// value — см. GlassApi.h: примитив как есть; объект/массив —
        /// только {"ref": "адрес", ...} — сами данные см. GetHeapJson().
        /// </summary>
        public string GetLocalsJson(int frameDepth = 0) =>
            ConsumeUtf8String(NativeMethods.glass_get_locals_json(_handle, frameDepth)) ?? "[]";

        /// <summary>
        /// {"objects": [...]} — снимок всей кучи для визуализации (см.
        /// GlassApi.h). "ref" в GetLocalsJson()/полях объектов и
        /// "address" здесь — один и тот же адрес: так строится граф
        /// (узлы — объекты/массивы, рёбра — поля/элементы), не
        /// разворачивается вглубь без ограничений (циклы в куче —
        /// не проблема).
        /// </summary>
        public string GetHeapJson() => ConsumeUtf8String(NativeMethods.glass_get_heap_json(_handle)) ?? "{\"objects\":[]}";

        /// <summary>
        /// Немедленная сборка мусора (как ~gc в языке). Возвращает, сколько
        /// объектов освобождено. Безопасно между шагами отладки.
        /// </summary>
        public int CollectGarbage() => NativeMethods.glass_collect_garbage(_handle);

        /// <summary>
        /// Устройство одного объекта кучи (раскладка полей, vtable, предки) —
        /// JSON формы из GlassApi.h (glass_get_object_info_json), или null,
        /// если по этому адресу нет живого объекта.
        /// </summary>
        public string? GetObjectInfoJson(string address)
        {
            string? json = ConsumeUtf8String(NativeMethods.glass_get_object_info_json(_handle, address));
            return json == null || json == "null" ? null : json;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            _handle.Dispose();
        }

        // Marshal.PtrToStringUTF8 сам не освобождает нативную память —
        // glass_free_string после чтения обязателен (контракт GlassApi.h:
        // каждая полученная char* — через glass_free_string, никогда
        // free()/delete — аллокатор на стороне native может быть не тем
        // же самым, что видит .NET, особенно между модулями на Windows).
        private static string? ConsumeUtf8String(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) return null;
            try
            {
                return Marshal.PtrToStringUTF8(ptr);
            }
            finally
            {
                NativeMethods.glass_free_string(ptr);
            }
        }
    }
}
