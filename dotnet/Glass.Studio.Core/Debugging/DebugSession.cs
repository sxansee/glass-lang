// Отладочная сессия: одна скомпилированная программа Glass + живая VM, вокруг
// которой построен весь отладчик IDE.
//
// Главная забота этого класса — потоки. Выполнение (run/step) в нативной
// библиотеке БЛОКИРУЕТ вызывающий поток, пока программа не дойдёт до паузы,
// а программа студента может быть `while true {}`. Поэтому команды выполняются
// на пуле потоков (UI остаётся живым, а кнопка «Стоп» работает), и действует
// одно правило: пока команда выполняется, нативную сессию НЕЛЬЗЯ трогать из
// других потоков, КРОМЕ RequestStop() (атомарный флаг на стороне VM). Правило
// не на словах — Capture()/CollectGarbage()/SetBreakpoints() во время
// выполнения бросают InvalidOperationException, а точки останова
// синхронизируются с нативной стороной только на старте команды.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Glass.Interop;
using Glass.Studio.Models;

namespace Glass.Studio.Debugging
{
    public enum SessionPhase
    {
        /// <summary>Скомпилировано, выполнение ещё не начиналось.</summary>
        Ready,
        /// <summary>Идёт команда (run/continue/step) — трогать сессию нельзя, кроме RequestStop().</summary>
        Running,
        /// <summary>Остановлено на точке останова или после шага — можно читать состояние.</summary>
        Paused,
        /// <summary>Программа завершилась (нормально, с ошибкой или по Стоп).</summary>
        Finished,
    }

    /// <summary>Итог одной команды отладчика.</summary>
    public sealed class CommandResult
    {
        public CommandResult(SessionPhase phase, GlassRunStatus status, string? runtimeError)
        {
            Phase = phase;
            Status = status;
            RuntimeError = runtimeError;
        }

        /// <summary>Paused или Finished.</summary>
        public SessionPhase Phase { get; }
        public GlassRunStatus Status { get; }
        /// <summary>Текст рантайм-ошибки/необработанного исключения/"выполнение прервано", или null.</summary>
        public string? RuntimeError { get; }
        public bool StoppedByUser => RuntimeError != null && RuntimeError.Contains("прерван", StringComparison.Ordinal);
    }

    /// <summary>Всё, что отладчик показывает на паузе, одним снимком.</summary>
    public sealed class DebugSnapshot
    {
        public DebugSnapshot(string output, int currentLine, IReadOnlyList<StackFrameInfo> callStack,
                             IReadOnlyList<FrameLocals> frames, HeapSnapshot heap)
        {
            Output = output;
            CurrentLine = currentLine;
            CallStack = callStack;
            Frames = frames;
            Heap = heap;
        }

        public string Output { get; }
        /// <summary>Строка, на которой стоит выполнение, или -1.</summary>
        public int CurrentLine { get; }
        public IReadOnlyList<StackFrameInfo> CallStack { get; }
        public IReadOnlyList<FrameLocals> Frames { get; }
        public HeapSnapshot Heap { get; }
    }

    public sealed class DebugSession : IDisposable
    {
        private readonly GlassSession _native;
        private readonly SortedSet<int> _breakpoints = new SortedSet<int>();
        // Завершается, когда текущая команда ПОЛНОСТЬЮ перестала трогать нативную сессию — включая хвост
        // после возврата из нативного вызова (чтение ошибки). Dispose ждёт именно его: ждать только сам
        // нативный вызов мало, продолжение команды ещё читает сессию, и освободить её под ним нельзя.
        private TaskCompletionSource<bool>? _commandDone;
        private int _phase = (int)SessionPhase.Ready;
        private int _disposed;

        private DebugSession(GlassSession native)
        {
            _native = native;
        }

        /// <summary>Бросает GlassCompileException с диагностикой первой ошибки.</summary>
        public static DebugSession Compile(string source) => new DebugSession(GlassSession.Compile(source));

        public SessionPhase Phase => (SessionPhase)Volatile.Read(ref _phase);

        /// <summary>
        /// Точки останова (номера строк, с 1). Применяются к VM в начале следующей команды —
        /// менять можно в любой момент, в том числе пока программа выполняется.
        /// </summary>
        public void SetBreakpoints(IEnumerable<int> lines)
        {
            lock (_breakpoints)
            {
                _breakpoints.Clear();
                foreach (int line in lines)
                {
                    if (line > 0) _breakpoints.Add(line);
                }
            }
        }

        /// <summary>Выполнить программу целиком, не останавливаясь на точках останова.</summary>
        public Task<CommandResult> RunAsync() => ExecuteAsync(() =>
        {
            _native.Run();  // успех/ошибку определяем по HasError в ExecuteAsync
            return GlassRunStatus.Finished;
        });

        /// <summary>Продолжить (или начать) до ближайшей точки останова/конца программы.</summary>
        public Task<CommandResult> ContinueAsync() => ExecuteAsync(_native.RunUntilPause);

        public Task<CommandResult> StepIntoAsync() => ExecuteAsync(_native.StepInto);

        public Task<CommandResult> StepOverAsync() => ExecuteAsync(_native.StepOver);

        public Task<CommandResult> StepOutAsync() => ExecuteAsync(_native.StepOut);

        /// <summary>
        /// Просит остановить выполняющуюся команду. Можно из любого потока;
        /// на ничего не выполняющей сессии безвредно (следующая команда сразу
        /// завершится как "прервано" — поэтому зовём только при Running).
        /// </summary>
        public void RequestStop()
        {
            if (Volatile.Read(ref _disposed) != 0 || Phase != SessionPhase.Running) return;
            try
            {
                _native.RequestStop();
            }
            catch (ObjectDisposedException)
            {
                // Сессию успели освободить между проверкой и вызовом — остановка уже не нужна.
            }
        }

        public string GetOutput()
        {
            EnsureNotRunning();
            return _native.GetOutput();
        }

        /// <summary>Немедленная сборка мусора; возвращает число освобождённых объектов. Только на паузе.</summary>
        public int CollectGarbage()
        {
            EnsureNotRunning();
            if (Phase != SessionPhase.Paused) throw new InvalidOperationException("Сборка мусора доступна только на паузе.");
            return _native.CollectGarbage();
        }

        /// <summary>Информация об одном объекте кучи (раскладка, vtable) — или null, если он уже собран.</summary>
        public ObjectInfo? InspectObject(string address)
        {
            EnsureNotRunning();
            return GlassJson.ParseObjectInfo(_native.GetObjectInfoJson(address));
        }

        /// <summary>
        /// Снимок состояния: стек вызовов, локальные переменные верхних maxFramesWithLocals
        /// фреймов (у более глубоких список пуст — на рекурсии в тысячи фреймов каждая пауза
        /// иначе читала бы тысячи JSON), вся куча, вывод.
        /// После нормального завершения фреймов нет (VM размотала стек); после рантайм-ошибки или
        /// «Стоп» стек и переменные сохраняются — состояние на момент сбоя.
        /// </summary>
        public DebugSnapshot Capture(int maxFramesWithLocals = 40)
        {
            EnsureNotRunning();
            IReadOnlyList<StackFrameInfo> stack = GlassJson.ParseCallStack(_native.GetCallStackJson());
            var frames = new List<FrameLocals>(stack.Count);
            for (int depth = 0; depth < stack.Count; depth++)
            {
                IReadOnlyList<LocalVariable> locals = depth < maxFramesWithLocals
                    ? GlassJson.ParseLocals(_native.GetLocalsJson(depth))
                    : Array.Empty<LocalVariable>();
                frames.Add(new FrameLocals(depth, stack[depth].Function, stack[depth].Line, locals));
            }
            HeapSnapshot heap = GlassJson.ParseHeap(_native.GetHeapJson());
            return new DebugSnapshot(_native.GetOutput(), _native.CurrentLine, stack, frames, heap);
        }

        private async Task<CommandResult> ExecuteAsync(Func<GlassRunStatus> nativeCall)
        {
            if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(DebugSession));
            SessionPhase current = Phase;
            if (current == SessionPhase.Running) throw new InvalidOperationException("Программа уже выполняется.");
            if (current == SessionPhase.Finished) throw new InvalidOperationException("Программа уже завершена — начните новую сессию.");

            // Точки останова — на вызывающем потоке и ДО старта: пока ничего не
            // выполняется, нативную сторону трогать безопасно.
            _native.ClearAllBreakpoints();
            lock (_breakpoints)
            {
                foreach (int line in _breakpoints) _native.SetBreakpoint(line);
            }

            // Сначала публикуем «команда идёт», и только потом переключаем фазу: иначе Dispose из другого
            // потока увидел бы Running, но не нашёл бы, чего ждать.
            var done = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            Volatile.Write(ref _commandDone, done);
            Volatile.Write(ref _phase, (int)SessionPhase.Running);

            try
            {
                GlassRunStatus status;
                try
                {
                    status = await Task.Run(nativeCall).ConfigureAwait(false);
                }
                catch
                {
                    Volatile.Write(ref _phase, (int)SessionPhase.Finished);
                    throw;
                }

                // Все обращения к нативной сессии — ДО смены фазы и до done: после этого её можно освобождать.
                string? error = _native.HasError ? _native.GetError() : null;
                SessionPhase next = status == GlassRunStatus.Finished || error != null ? SessionPhase.Finished : SessionPhase.Paused;
                Volatile.Write(ref _phase, (int)next);
                return new CommandResult(next, status, error);
            }
            finally
            {
                done.TrySetResult(true);
            }
        }

        private void EnsureNotRunning()
        {
            if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(DebugSession));
            if (Phase == SessionPhase.Running)
            {
                throw new InvalidOperationException("Нативную сессию нельзя читать, пока выполняется команда (кроме RequestStop).");
            }
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
            TaskCompletionSource<bool>? running = Volatile.Read(ref _commandDone);
            if (Phase == SessionPhase.Running && running != null)
            {
                // Выход из IDE посреди выполнения: сначала остановим VM, дождёмся, пока команда целиком
                // отпустит сессию (возврат из нативного вызова И чтение результата), и только потом
                // освободим её — освобождение под ногами у работающего потока было бы падением процесса.
                try { _native.RequestStop(); } catch (ObjectDisposedException) { }
                if (!running.Task.Wait(TimeSpan.FromSeconds(3))) return;  // не отпустила — лучше утечка, чем падение
            }
            _native.Dispose();
        }
    }
}
