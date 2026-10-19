using System;
using System.Linq;
using System.Threading.Tasks;
using Glass.Interop;
using Glass.Studio.Debugging;
using Glass.Studio.Examples;
using Glass.Studio.Models;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>DebugSession против живой glass_capi: потоки, паузы, шаги, остановка, снимки.</summary>
    public static class SessionTests
    {
        // Нумерация строк важна: тесты ставят точки останова по этим номерам.
        internal const string Stepping =
            "fn add(a: int, b: int) -> int {\n" +   // 1
            "    let s = a + b\n" +                  // 2
            "    return s\n" +                       // 3
            "}\n" +                                  // 4
            "\n" +                                   // 5
            "fn main() {\n" +                        // 6
            "    let x = add(1, 2)\n" +              // 7
            "    print(x)\n" +                       // 8
            "}\n";                                   // 9

        internal const string Forever =
            "fn main() {\n" +
            "    let i = 0\n" +
            "    while true {\n" +
            "        i += 1\n" +
            "    }\n" +
            "}\n";

        internal const string Animals =
            "mold Animal {\n" +                                           // 1
            "    let name: string\n" +                                    // 2
            "    let age: int\n" +                                        // 3
            "    Animal(n: string, a: int) { name = n; age = a; }\n" +    // 4
            "    virtual fn speak() -> string { return \"...\"; }\n" +    // 5
            "    virtual fn eat() -> string { return \"ням\"; }\n" +      // 6 (в vtable попадают только virtual-методы)
            "}\n" +                                                       // 7
            "mold Dog : Animal {\n" +                                     // 8
            "    let breed: string\n" +                                   // 9
            "    Dog(n: string, a: int, b: string) { super(n, a); breed = b; }\n" +   // 10
            "    override fn speak() -> string { return \"Гав\"; }\n" +   // 11
            "}\n" +                                                       // 12
            "fn main() {\n" +                                             // 13
            "    let d = Dog(\"Рекс\", 3, \"Лабрадор\")\n" +              // 14
            "    let xs = [1, 2, 3]\n" +                                  // 15
            "    print(d.speak())\n" +                                    // 16
            "}\n";                                                        // 17

        internal const string GcSource =
            "mold Box {\n" +                      // 1
            "    let value: int\n" +             // 2
            "    Box(v: int) { value = v; }\n" + // 3
            "}\n" +                              // 4
            "fn main() {\n" +                     // 5
            "    let a = Box(1)\n" +             // 6
            "    let b = Box(2)\n" +             // 7
            "    a = b\n" +                      // 8
            "    ~gc\n" +                        // 9
            "    print(a.value)\n" +             // 10
            "}\n";                               // 11

        public static void Register()
        {
            Add("сессия: ошибка компиляции с позицией", CompileError);
            Add("сессия: Run до конца, вывод", RunToEnd);
            Add("сессия: точка останова, снимок, продолжение", BreakpointAndCapture);
            Add("сессия: шаги into/over/out и стек вызовов", Stepping_);
            Add("сессия: Стоп прерывает бесконечный цикл", StopInfiniteLoop);
            Add("сессия: во время выполнения читать и запускать второй раз нельзя", GuardsWhileRunning);
            Add("сессия: Dispose во время выполнения не падает", DisposeWhileRunning);
            Add("сессия: состояние на момент рантайм-ошибки сохраняется", RuntimeErrorKeepsState);
            Add("сессия: сборка мусора освобождает недостижимое", CollectGarbage);
            Add("сессия: инспектор объекта — раскладка и vtable", InspectObject);
            Add("сессия: глубокая рекурсия — локали только у верхних фреймов", DeepRecursion);
            Add("сессия: бесконечная рекурсия — ошибка, а не падение", InfiniteRecursion);
            Add("сессия: служебный код пролога невидим для отладчика", PreludeInvisible);
            Add("примеры: каталог совпадает со встроенными, все компилируются и выполняются", Examples);
        }

        private static async Task WaitUntil(Func<bool> condition, int timeoutMs = 5000)
        {
            for (int waited = 0; waited < timeoutMs && !condition(); waited += 10) await Task.Delay(10);
        }

        private static void CompileError()
        {
            GlassCompileException? caught = null;
            try { DebugSession.Compile("fn main( {\n}\n"); }
            catch (GlassCompileException ex) { caught = ex; }
            Check(caught != null, "GlassCompileException брошено");
            if (caught == null) return;
            Diagnostic d = DiagnosticParser.ParseCompileError(caught.Message);
            CheckEqual(1, d.Line, "строка ошибки разобрана");
            Check(d.Column > 0, "столбец ошибки разобран");
            Check(d.Message.Length > 0, "сообщение не пустое");

            // Типовая ошибка (сема), не только синтаксис.
            try { DebugSession.Compile("fn main() {\n    let x: int = \"hi\"\n}\n"); Check(false, "ошибка типа должна быть поймана"); }
            catch (GlassCompileException ex) { CheckEqual(2, DiagnosticParser.ParseCompileError(ex.Message).Line, "строка ошибки типа"); }
        }

        private static async Task RunToEnd()
        {
            using DebugSession s = DebugSession.Compile(Stepping);
            CheckEqual(SessionPhase.Ready, s.Phase, "после компиляции — Ready");
            CommandResult r = await s.RunAsync();
            CheckEqual(SessionPhase.Finished, r.Phase, "Run завершает программу");
            CheckEqual<string?>(null, r.RuntimeError, "без ошибки");
            CheckEqual("3", s.GetOutput(), "вывод программы");
            CheckThrows<InvalidOperationException>(() => s.ContinueAsync().GetAwaiter().GetResult(), "после завершения команды запрещены");
        }

        private static async Task BreakpointAndCapture()
        {
            using DebugSession s = DebugSession.Compile(Stepping);
            s.SetBreakpoints(new[] { 8, -3, 0 });   // мусорные номера игнорируются
            CommandResult r = await s.ContinueAsync();
            CheckEqual(SessionPhase.Paused, r.Phase, "остановились");
            CheckEqual(GlassRunStatus.PausedAtBreakpoint, r.Status, "на точке останова");

            DebugSnapshot snap = s.Capture();
            CheckEqual(8, snap.CurrentLine, "строка паузы");
            CheckEqual(1, snap.CallStack.Count, "стек: только main");
            CheckEqual("main", snap.CallStack[0].Function, "имя функции");
            LocalVariable x = snap.Frames[0].Locals.First(l => l.Name == "x");
            CheckEqual("3", x.Value.Display, "x == 3 к строке 8");
            CheckEqual("", snap.Output, "print(x) ещё не выполнился");

            r = await s.ContinueAsync();
            CheckEqual(SessionPhase.Finished, r.Phase, "дальше точек нет — конец");
            CheckEqual("3", s.GetOutput(), "вывод после продолжения");
        }

        private static async Task Stepping_()
        {
            using DebugSession s = DebugSession.Compile(Stepping);
            CommandResult r = await s.StepIntoAsync();
            CheckEqual(SessionPhase.Paused, r.Phase, "первый шаг — пауза");
            CheckEqual(7, s.Capture().CurrentLine, "встали на первой строке main");

            r = await s.StepIntoAsync();
            DebugSnapshot inside = s.Capture();
            CheckEqual(SessionPhase.Paused, r.Phase, "зашли в add");
            CheckEqual(2, inside.CallStack.Count, "стек: add и main");
            CheckEqual("add", inside.CallStack[0].Function, "верхний фрейм — add");
            CheckEqual(2, inside.CurrentLine, "первая строка add");
            CheckEqual("1", inside.Frames[0].Locals.First(l => l.Name == "a").Value.Display, "аргумент a");
            CheckEqual("main", inside.CallStack[1].Function, "ниже — main");

            r = await s.StepOutAsync();
            DebugSnapshot back = s.Capture();
            CheckEqual(1, back.CallStack.Count, "вышли в main");
            CheckEqual("main", back.CallStack[0].Function, "снова main");

            // Шагаем по строкам до конца — конечное число шагов.
            int steps = 0;
            while (s.Phase == SessionPhase.Paused && steps < 20)
            {
                await s.StepOverAsync();
                steps++;
            }
            CheckEqual(SessionPhase.Finished, s.Phase, "в итоге программа завершилась");
            Check(steps < 20, "шагов конечное число (" + steps + ")");
            CheckEqual("3", s.GetOutput(), "вывод");
        }

        private static async Task StopInfiniteLoop()
        {
            using DebugSession s = DebugSession.Compile(Forever);
            Task<CommandResult> run = s.ContinueAsync();
            await WaitUntil(() => s.Phase == SessionPhase.Running);
            CheckEqual(SessionPhase.Running, s.Phase, "цикл крутится");
            await Task.Delay(50);
            Check(!run.IsCompleted, "команда не завершилась сама");
            s.RequestStop();
            Task finished = await Task.WhenAny(run, Task.Delay(5000));
            Check(ReferenceEquals(finished, run), "Стоп сработал за 5 с");
            if (!run.IsCompleted) return;
            CommandResult r = await run;
            CheckEqual(SessionPhase.Finished, r.Phase, "после Стоп — Finished");
            Check(r.StoppedByUser, "помечено как остановленное пользователем");
            CheckContains(r.RuntimeError ?? "", "прерван", "сообщение «выполнение прервано»");

            // RequestStop на уже остановленной сессии — безвредно.
            s.RequestStop();
        }

        private static async Task GuardsWhileRunning()
        {
            using DebugSession s = DebugSession.Compile(Forever);
            Task<CommandResult> run = s.ContinueAsync();
            await WaitUntil(() => s.Phase == SessionPhase.Running);
            CheckThrows<InvalidOperationException>(() => s.Capture(), "Capture во время выполнения");
            CheckThrows<InvalidOperationException>(() => s.GetOutput(), "GetOutput во время выполнения");
            CheckThrows<InvalidOperationException>(() => s.CollectGarbage(), "CollectGarbage во время выполнения");
            CheckThrows<InvalidOperationException>(() => s.ContinueAsync().GetAwaiter().GetResult(), "вторая команда параллельно");
            s.SetBreakpoints(new[] { 4 });   // менять точки можно в любой момент — уйдут со следующей командой
            s.RequestStop();
            await run;
            CheckEqual(SessionPhase.Finished, s.Phase, "после остановки — Finished");
        }

        private static async Task DisposeWhileRunning()
        {
            // Гонка Dispose с хвостом команды ловится только повторами: 40 раз подряд, по-разному
            // «в фазе» (с паузой и без), и каждый раз команда обязана завершиться штатно — без
            // ObjectDisposedException из продолжения, которое ещё читало освобождаемую сессию.
            for (int round = 0; round < 40; round++)
            {
                DebugSession s = DebugSession.Compile(Forever);
                Task<CommandResult> run = s.ContinueAsync();
                await WaitUntil(() => s.Phase == SessionPhase.Running);
                if (round % 3 == 1) await Task.Delay(round % 7);

                var watch = System.Diagnostics.Stopwatch.StartNew();
                s.Dispose();   // сам останавливает VM и ждёт, пока команда целиком отпустит сессию
                watch.Stop();
                Check(watch.ElapsedMilliseconds < 4000, "раунд " + round + ": Dispose вернулся быстро (" + watch.ElapsedMilliseconds + " мс)");

                // Команда завершается не «мгновенно после Dispose», а вскоре: ждём с запасом, не гадаем.
                Task finished = await Task.WhenAny(run, Task.Delay(5000));
                Check(ReferenceEquals(finished, run), "раунд " + round + ": команда завершилась");
                if (!run.IsCompleted) return;
                CommandResult result = await run;   // исключение из продолжения проявилось бы здесь
                Check(result.Phase == SessionPhase.Finished && result.StoppedByUser, "раунд " + round + ": завершена как остановленная");

                s.Dispose();   // повторный Dispose безвреден
                if (round == 0) CheckThrows<ObjectDisposedException>(() => s.Capture(), "после Dispose сессия недоступна");
            }
        }

        private static async Task RuntimeErrorKeepsState()
        {
            using DebugSession s = DebugSession.Compile(
                "mold Box {\n" +
                "    let v: int\n" +
                "    Box(x: int) { v = x; }\n" +
                "}\n" +
                "fn boom(xs: [int]) -> int {\n" +
                "    let b = Box(5)\n" +
                "    return xs[9]\n" +             // 7: падает здесь
                "}\n" +
                "fn main() {\n" +
                "    let a = [1, 2]\n" +
                "    print(boom(a))\n" +
                "}\n");
            CommandResult r = await s.ContinueAsync();
            CheckEqual(SessionPhase.Finished, r.Phase, "программа упала");
            Check(r.RuntimeError != null && r.RuntimeError.Contains("вне границ", StringComparison.Ordinal), "текст ошибки");
            Check(!r.StoppedByUser, "это не «Стоп»");
            CheckEqual(7, DiagnosticParser.ParseRuntimeError(r.RuntimeError ?? "").Line, "строка ошибки");

            DebugSnapshot crash = s.Capture();
            CheckEqual(2, crash.CallStack.Count, "стек на момент сбоя сохранён: boom, main");
            CheckEqual("boom", crash.CallStack[0].Function, "упали в boom");
            CheckEqual(2, crash.Heap.Objects.Count, "в куче Box и массив");
        }

        private static async Task CollectGarbage()
        {
            using DebugSession s = DebugSession.Compile(GcSource);
            s.SetBreakpoints(new[] { 9 });
            CommandResult r = await s.ContinueAsync();
            CheckEqual(SessionPhase.Paused, r.Phase, "дошли до ~gc");

            HeapSnapshot before = s.Capture().Heap;
            CheckEqual(2, before.Objects.Count, "два объекта в куче");
            CheckEqual(1, before.Objects.Count(o => !o.Reachable), "один из них уже мусор (Box(1))");
            CheckEqual(1, s.CollectGarbage(), "собран один объект");
            HeapSnapshot after = s.Capture().Heap;
            CheckEqual(1, after.Objects.Count, "остался один");
            Check(after.Objects.All(o => o.Reachable), "и он живой");
            CheckEqual(0, s.CollectGarbage(), "повторно собирать нечего");

            r = await s.ContinueAsync();
            CheckEqual(SessionPhase.Finished, r.Phase, "программа доработала");
            CheckEqual("2", s.GetOutput(), "живой объект не пострадал");
        }

        private static async Task InspectObject()
        {
            using DebugSession s = DebugSession.Compile(Animals);
            s.SetBreakpoints(new[] { 16 });
            await s.ContinueAsync();
            DebugSnapshot snap = s.Capture();
            LocalVariable d = snap.Frames[0].Locals.First(l => l.Name == "d");
            LocalVariable xs = snap.Frames[0].Locals.First(l => l.Name == "xs");

            ObjectInfo? info = s.InspectObject(d.Value.Address!);
            Check(info != null, "объект найден");
            if (info == null) return;
            CheckEqual("Dog", info.ClassName, "класс");
            CheckEqual("Dog,Animal", string.Join(",", info.Ancestry), "предки");
            CheckEqual("name,age,breed", string.Join(",", info.Layout.Select(f => f.Name)), "унаследованные поля первыми");
            CheckEqual("0,1,2", string.Join(",", info.Layout.Select(f => f.Offset)), "смещения");
            CheckEqual("ref,int,ref", string.Join(",", info.Layout.Select(f => f.Type)), "типы");
            VTableEntry speak = info.VTable.First(e => e.Method == "speak");
            CheckEqual("Dog", speak.ImplementedBy, "speak переопределён в Dog");
            VTableEntry eat = info.VTable.First(e => e.Method == "eat");
            CheckEqual("Animal", eat.ImplementedBy, "eat унаследован от Animal");

            ObjectInfo? array = s.InspectObject(xs.Value.Address!);
            Check(array != null && array.IsArray && array.Length == 3, "массив из 3 элементов");
            Check(s.InspectObject("12345") == null, "несуществующий адрес — null");
            Check(s.InspectObject("не число") == null, "не адрес — null, без падения");
        }

        private static async Task DeepRecursion()
        {
            using DebugSession s = DebugSession.Compile(
                "fn down(n: int) -> int {\n" +
                "    if n == 0 {\n" +
                "        return 0\n" +        // 3
                "    }\n" +
                "    return down(n - 1)\n" +
                "}\n" +
                "fn main() {\n" +
                "    print(down(100))\n" +
                "}\n");
            s.SetBreakpoints(new[] { 3 });
            await s.ContinueAsync();
            DebugSnapshot snap = s.Capture(maxFramesWithLocals: 40);
            CheckEqual(102, snap.CallStack.Count, "down ×101 и main");
            CheckEqual(1, snap.Frames[0].Locals.Count, "верхний фрейм — с локалями");
            CheckEqual(0, snap.Frames[60].Locals.Count, "глубже лимита — локали не читаются");
            CheckEqual("main", snap.Frames[101].Function, "дно стека — main");
        }

        private static async Task InfiniteRecursion()
        {
            using DebugSession s = DebugSession.Compile(
                "fn f(n: int) -> int {\n    return f(n + 1)\n}\nfn main() {\n    print(f(0))\n}\n");
            CommandResult r = await s.RunAsync();
            CheckEqual(SessionPhase.Finished, r.Phase, "рекурсия завершилась ошибкой");
            CheckContains(r.RuntimeError ?? "", "переполнение стека вызовов", "понятное сообщение");
        }

        private static async Task PreludeInvisible()
        {
            // Строка 5 пользователя не выполняется, но ТАКАЯ ЖЕ строка есть в служебном коде Exception.
            using DebugSession s = DebugSession.Compile(
                "fn main() {\n" +
                "    let x = 0\n" +
                "    if x == 1 {\n" +
                "        print(\"never\")\n" +
                "        print(\"never 2\")\n" +      // 5
                "    }\n" +
                "    let e = Exception(\"boom\")\n" +
                "    print(e.message)\n" +
                "}\n");
            s.SetBreakpoints(new[] { 5 });
            CommandResult r = await s.ContinueAsync();
            CheckEqual(SessionPhase.Finished, r.Phase, "точка на невыполняемой строке не срабатывает в чужом коде");
            CheckEqual("boom", s.GetOutput(), "программа доработала");
        }

        private static async Task Examples()
        {
            var catalogIds = ExampleCatalog.All.Select(e => e.Id).OrderBy(x => x, StringComparer.Ordinal).ToList();
            var embedded = ExampleCatalog.EmbeddedIds().ToList();
            CheckEqual(string.Join(",", embedded), string.Join(",", catalogIds), "каталог = встроенные ресурсы");

            foreach (ExampleItem item in ExampleCatalog.All)
            {
                string source = ExampleCatalog.LoadSource(item.Id);
                Check(source.Length > 50, item.Id + ": исходник загружен");
                using DebugSession s = DebugSession.Compile(source);
                CommandResult r = await s.RunAsync();
                CheckEqual<string?>(null, r.RuntimeError, item.Id + ": выполнился без ошибок");
                Check(s.GetOutput().Length > 0, item.Id + ": что-то напечатал");
            }
            CheckThrows<System.IO.FileNotFoundException>(() => ExampleCatalog.LoadSource("нет-такого"), "неизвестный пример");
        }
    }
}
