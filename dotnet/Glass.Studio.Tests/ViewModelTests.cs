using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using Glass.Studio.Examples;
using Glass.Studio.Models;
using Glass.Studio.Mvvm;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// MainViewModel целиком: настоящая VM, заглушки редактора и диалогов. Проверяется то, что
    /// увидит пользователь: когда какая кнопка доступна, что в статусной строке, что подсвечено
    /// в редакторе, что в таблицах и на схеме кучи.
    /// </summary>
    public static class ViewModelTests
    {
        public static void Register()
        {
            Add("IDE: F5 до точки останова, продолжение до конца", BreakpointFlow);
            Add("IDE: шаги, стек вызовов, переключение фрейма", StepFlow);
            Add("IDE: ошибка компиляции — в «Ошибки», в редактор, остаёмся в Editing", CompileErrorFlow);
            Add("IDE: ошибка выполнения — состояние на момент сбоя сохранено", RuntimeErrorFlow);
            Add("IDE: Стоп прерывает бесконечный цикл", StopFlow);
            Add("IDE: сборка мусора — мусор виден и исчезает", GarbageFlow);
            Add("IDE: инспектор — раскладка, vtable, массив, выбор по переменной", InspectorFlow);
            Add("IDE: доступность команд по состояниям", CommandAvailability);
            Add("IDE: точки останова", Breakpoints);
            Add("IDE: правка кода на паузе помечает программу устаревшей", StaleSource);
            Add("IDE: файлы — сохранить, открыть, подтверждение потери", Files);
            Add("IDE: примеры запускаются из меню", ExamplesFlow);
            Add("IDE: проверка кода без запуска", LiveCheck);
            Add("IDE: повторные запуски и закрытие сессии", Restart);
            Add("IDE: куча остаётся на экране после нормального завершения", HeapAfterFinish);
            Add("IDE: сводка кучи и склонения", HeapSummary);
            Add("IDE: меню примеров и перенос точек останова", MenuAndBreakpointMoves);
        }

        private sealed class Harness
        {
            public readonly FakeEditor Editor = new FakeEditor();
            public readonly FakeDialogs Dialogs = new FakeDialogs();
            public readonly MainViewModel Vm;

            public Harness(string source)
            {
                Editor.Text = source;
                Vm = new MainViewModel(Editor, Dialogs, liveCheckDelayMs: -1);
            }
        }

        private static async Task WaitUntil(Func<bool> condition, int timeoutMs = 5000)
        {
            for (int waited = 0; waited < timeoutMs && !condition(); waited += 10) await Task.Delay(10);
        }

        private static async Task BreakpointFlow()
        {
            var h = new Harness(SessionTests.Stepping);
            CheckEqual(IdeState.Editing, h.Vm.State, "стартуем в Editing");
            h.Vm.ToggleBreakpoint(8);
            CheckEqual("8", string.Join(",", h.Editor.Breakpoints), "редактор получил точку останова");

            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Paused, h.Vm.State, "остановились");
            CheckContains(h.Vm.StatusText, "точке останова", "статус");
            CheckContains(h.Vm.StatusText, "8", "статус содержит номер строки");
            CheckEqual(8, h.Editor.ExecutionLine, "редактор подсветил строку 8");
            CheckEqual(ExecutionMarker.Current, h.Editor.Marker, "маркер текущей строки");
            CheckEqual(1, h.Vm.CallStack.Count, "стек: main");
            LocalItem x = h.Vm.Locals.First(l => l.Name == "x");
            CheckEqual("3", x.ValueText, "x == 3");
            CheckEqual("int", x.TypeText, "тип x");
            CheckEqual("", h.Vm.Output, "вывода пока нет");
            Check(h.Vm.IsPaused && !h.Vm.IsRunning && h.Vm.HasSession, "флаги состояния для привязок");

            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Finished, h.Vm.State, "дошли до конца");
            CheckEqual("3", h.Vm.Output, "вывод программы");
            CheckEqual<int?>(null, h.Editor.ExecutionLine, "подсветка снята");
            CheckEqual("Программа завершена", h.Vm.StatusText, "статус");
            Check(!h.Vm.HasProblems, "ошибок нет");
        }

        private static async Task StepFlow()
        {
            var h = new Harness(SessionTests.Stepping);
            await h.Vm.StepIntoAsync();   // без сессии: начать и встать на первой строке main
            CheckEqual(IdeState.Paused, h.Vm.State, "пауза после первого шага");
            CheckEqual(7, h.Editor.ExecutionLine, "строка 7");

            await h.Vm.StepIntoAsync();
            CheckEqual(2, h.Vm.CallStack.Count, "вошли в add");
            CheckEqual("add", h.Vm.CallStack[0].Function, "верхний фрейм");
            CheckEqual("add", h.Vm.SelectedFrame?.Function, "выбран верхний фрейм");
            CheckEqual(2, h.Editor.ExecutionLine, "строка внутри add");
            Check(h.Vm.Locals.Any(l => l.Name == "a"), "видны аргументы add");

            // Клик по вызывающему фрейму: переменные main и зелёная строка вызова.
            h.Vm.SelectedFrame = h.Vm.CallStack[1];
            Check(h.Vm.Locals.Any(l => l.Name == "x") && !h.Vm.Locals.Any(l => l.Name == "a"), "локали main, а не add");
            CheckEqual(7, h.Editor.ExecutionLine, "подсвечена строка вызова");
            CheckEqual(ExecutionMarker.Caller, h.Editor.Marker, "маркер вызывающего фрейма");
            h.Vm.SelectedFrame = h.Vm.CallStack[0];
            CheckEqual(ExecutionMarker.Current, h.Editor.Marker, "обратно на текущий");

            await h.Vm.StepOutAsync();
            CheckEqual(1, h.Vm.CallStack.Count, "вышли из add");
            int steps = 0;
            while (h.Vm.State == IdeState.Paused && steps < 10)
            {
                await h.Vm.StepOverAsync();
                steps++;
            }
            CheckEqual(IdeState.Finished, h.Vm.State, "дошагали до конца");
            Check(steps >= 2 && steps < 10, "за разумное число шагов (" + steps + ")");
            CheckEqual("3", h.Vm.Output, "вывод");
        }

        private static async Task CompileErrorFlow()
        {
            var h = new Harness("fn main() {\n    let x: int = \"hi\"\n}\n");
            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Editing, h.Vm.State, "программа не запущена");
            CheckEqual(1, h.Vm.Problems.Count, "одна ошибка");
            CheckEqual(2, h.Vm.Problems[0].Line, "строка ошибки");
            CheckEqual(DiagnosticKind.Compile, h.Vm.Problems[0].Kind, "вид: компиляция");
            CheckEqual(1, h.Editor.Problems.Count, "редактор получил ошибку для подчёркивания");
            CheckEqual(2, h.Editor.LastGoTo, "курсор переведён на строку ошибки");
            CheckContains(h.Vm.StatusText, "Ошибка компиляции", "статус");
            CheckEqual(1, h.Vm.SelectedBottomTab, "открыта вкладка «Ошибки»");
            CheckEqual("Ошибки (1)", h.Vm.ProblemsHeader, "заголовок вкладки");

            // Клик по ошибке в списке переводит курсор.
            h.Editor.LastGoTo = null;
            h.Vm.SelectedProblem = h.Vm.Problems[0];
            CheckEqual(2, h.Editor.LastGoTo, "клик по ошибке — переход к строке");

            // Исправили — ошибки исчезают после запуска.
            h.Editor.Text = SessionTests.Stepping;
            await h.Vm.RunWithoutDebuggingAsync();
            Check(!h.Vm.HasProblems && h.Editor.Problems.Count == 0, "после исправления ошибок нет");
            CheckEqual("Ошибки", h.Vm.ProblemsHeader, "заголовок без счётчика");
            CheckEqual("3", h.Vm.Output, "и программа выполнилась");
        }

        private static async Task RuntimeErrorFlow()
        {
            var h = new Harness(
                "fn boom(xs: [int]) -> int {\n" +
                "    let k = 9\n" +
                "    return xs[k]\n" +             // 3
                "}\n" +
                "fn main() {\n" +
                "    let a = [1, 2]\n" +
                "    print(boom(a))\n" +
                "}\n");
            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Finished, h.Vm.State, "программа упала");
            CheckEqual(1, h.Vm.Problems.Count, "ошибка выполнения");
            CheckEqual(DiagnosticKind.Runtime, h.Vm.Problems[0].Kind, "вид: выполнение");
            CheckEqual(3, h.Vm.Problems[0].Line, "строка падения");
            CheckEqual(3, h.Editor.ExecutionLine, "красная строка в редакторе");
            CheckEqual(ExecutionMarker.Error, h.Editor.Marker, "маркер ошибки");
            CheckContains(h.Vm.StatusText, "Ошибка выполнения", "статус");
            CheckEqual(2, h.Vm.CallStack.Count, "стек на момент падения: boom, main");
            CheckEqual("9", h.Vm.Locals.First(l => l.Name == "k").ValueText, "видно, чему равнялось k");
            Check(!h.Vm.IsHeapStale, "куча — актуальная, на момент падения");
            CheckEqual(1, h.Vm.HeapGraph.Nodes.Count, "массив виден на схеме");
        }

        private static async Task StopFlow()
        {
            var h = new Harness(SessionTests.Forever);
            Task run = h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Running, h.Vm.State, "состояние Running выставлено сразу");
            CheckEqual("Выполняется…", h.Vm.StatusText, "статус");
            await Task.Delay(100);
            Check(!run.IsCompleted, "программа крутится");

            h.Vm.Stop();
            Task done = await Task.WhenAny(run, Task.Delay(5000));
            Check(ReferenceEquals(done, run), "Стоп сработал");
            await run;
            CheckEqual(IdeState.Finished, h.Vm.State, "после Стоп — Finished");
            CheckContains(h.Vm.StatusText, "Остановлено пользователем", "статус");
            Check(h.Vm.CallStack.Count >= 1, "видно, где остановили");
            CheckEqual(ExecutionMarker.Error, h.Editor.Marker, "строка, где остановили");

            h.Vm.Stop();   // на завершённой — закрывает сессию
            CheckEqual(IdeState.Editing, h.Vm.State, "второй Стоп закрыл сессию");
        }

        private static async Task GarbageFlow()
        {
            var h = new Harness(SessionTests.GcSource);
            h.Vm.ToggleBreakpoint(9);
            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Paused, h.Vm.State, "дошли до ~gc");
            CheckEqual(1, h.Vm.HeapGraph.GarbageCount, "один объект — мусор");
            CheckContains(h.Vm.HeapSummary, "мусор: 1", "сводка кучи");
            CheckEqual(2, h.Vm.HeapGraph.Nodes.Count, "оба объекта ещё на схеме");
            Check(h.Vm.HeapGraph.Nodes.Any(n => n.IsGarbage), "один помечен как мусор");

            int freed = h.Vm.CollectGarbage();
            CheckEqual(1, freed, "освобождён один объект");
            CheckEqual("Собрано 1 объект", h.Vm.GcMessage, "сообщение об итоге");
            CheckEqual(0, h.Vm.HeapGraph.GarbageCount, "мусора не осталось");
            CheckEqual(1, h.Vm.HeapGraph.Nodes.Count, "объект исчез со схемы");
            CheckEqual(IdeState.Paused, h.Vm.State, "остаёмся на паузе");
            CheckEqual(9, h.Editor.ExecutionLine, "строка выполнения не сбита");

            CheckEqual(0, h.Vm.CollectGarbage(), "второй сбор — нечего");
            CheckEqual("Собирать нечего — мусора нет", h.Vm.GcMessage, "сообщение");

            await h.Vm.StartOrContinueAsync();
            CheckEqual("2", h.Vm.Output, "программа доработала с живым объектом");
            CheckEqual(-1, h.Vm.CollectGarbage(), "после завершения собирать нельзя");
        }

        private static async Task InspectorFlow()
        {
            var h = new Harness(SessionTests.Animals);
            h.Vm.ToggleBreakpoint(16);
            await h.Vm.StartOrContinueAsync();
            Check(!h.Vm.Inspector.HasSelection, "пока ничего не выбрано");

            h.Vm.SelectedLocal = h.Vm.Locals.First(l => l.Name == "d");   // клик по ссылочной переменной
            ObjectInspectorViewModel insp = h.Vm.Inspector;
            Check(insp.HasSelection && insp.IsObject && !insp.IsArray, "выбран объект");
            CheckEqual("Dog", insp.Title, "заголовок");
            CheckEqual("Dog → Animal", insp.AncestryText, "предки");
            CheckContains(insp.AddressText, "0x", "адрес в hex");
            CheckContains(insp.AddressText, "~address", "и то же число, что вернёт ~address");
            CheckEqual(3, insp.Fields.Count, "три поля");
            CheckEqual("name,age,breed", string.Join(",", insp.Fields.Select(f => f.Name)), "унаследованные первыми");
            CheckEqual("0,1,2", string.Join(",", insp.Fields.Select(f => f.Offset)), "смещения");
            CheckEqual("3", insp.Fields[1].ValueText, "значение age");
            CheckEqual("слот 1 · 8 Б", insp.Fields[1].SlotText, "подпись слота для карты памяти");
            CheckContains(insp.SizeSummary, "3 поля", "сводка размера");
            VTableRow speak = insp.VTable.First(r => r.Method == "speak");
            VTableRow eat = insp.VTable.First(r => r.Method == "eat");
            Check(!speak.IsInherited && speak.Target == "Dog.speak", "speak переопределён в Dog");
            Check(eat.IsInherited && eat.Target == "Animal.eat", "eat унаследован");
            Check(h.Vm.HeapGraph.FindNode(h.Vm.Locals.First(l => l.Name == "d").Address)!.IsSelected, "узел выделен на схеме");

            h.Vm.SelectedLocal = h.Vm.Locals.First(l => l.Name == "xs");   // массив
            Check(insp.IsArray && !insp.IsObject, "теперь выбран массив");
            CheckEqual("array[3]", insp.Title, "заголовок массива");
            CheckEqual(3, insp.Elements.Count, "три элемента");
            CheckEqual("[0]", insp.Elements[0].Index, "индекс");
            CheckEqual("2", insp.Elements[1].ValueText, "значение элемента");

            // Выбор по клику на узле схемы (команда принимает адрес).
            string address = h.Vm.HeapGraph.Nodes.First(n => !n.IsArray).Address;
            h.Vm.SelectHeapNodeCommand.Execute(address);
            CheckEqual("Dog", insp.Title, "выбор через команду");

            h.Vm.SelectHeapNode(null);
            Check(!insp.HasSelection, "снятие выбора очищает инспектор");

            // Выбранный объект переживает шаг.
            h.Vm.SelectHeapNode(address);
            await h.Vm.StepOverAsync();
            Check(h.Vm.Inspector.HasSelection && h.Vm.Inspector.Title == "Dog", "выбор пережил шаг");
        }

        private static async Task CommandAvailability()
        {
            var h = new Harness(SessionTests.Stepping);
            h.Vm.ToggleBreakpoint(8);
            Check(Can(h.Vm.StartOrContinueCommand), "Editing: F5 доступен");
            Check(Can(h.Vm.StepIntoCommand) && Can(h.Vm.StepOverCommand), "Editing: шаги доступны (начать с первой строки)");
            Check(Can(h.Vm.RunWithoutDebuggingCommand), "Editing: Ctrl+F5 доступен");
            Check(!Can(h.Vm.StepOutCommand), "Editing: шаг с выходом недоступен");
            Check(!Can(h.Vm.StopCommand), "Editing: Стоп недоступен");
            Check(!Can(h.Vm.CollectGarbageCommand), "Editing: сборка мусора недоступна");
            Check(Can(h.Vm.ClearBreakpointsCommand), "есть точки — «снять все» доступно");

            await h.Vm.StartOrContinueAsync();
            Check(Can(h.Vm.StepOutCommand), "Paused: шаг с выходом доступен");
            Check(Can(h.Vm.StopCommand) && Can(h.Vm.CollectGarbageCommand), "Paused: Стоп и сборка мусора доступны");
            Check(!Can(h.Vm.RunWithoutDebuggingCommand), "Paused: Ctrl+F5 недоступен (программа уже запущена)");

            h.Vm.Stop();
            CheckEqual(IdeState.Editing, h.Vm.State, "Стоп на паузе закрывает сессию");
            Check(!Can(h.Vm.StepOutCommand) && !Can(h.Vm.StopCommand), "снова Editing");

            // Во время выполнения доступен только Стоп.
            var loop = new Harness(SessionTests.Forever);
            Task run = loop.Vm.StartOrContinueAsync();
            Check(!Can(loop.Vm.StartOrContinueCommand), "Running: F5 недоступен");
            Check(!Can(loop.Vm.StepIntoCommand) && !Can(loop.Vm.StepOverCommand) && !Can(loop.Vm.StepOutCommand), "Running: шаги недоступны");
            Check(!Can(loop.Vm.NewCommand) && !Can(loop.Vm.OpenCommand), "Running: «Создать/Открыть» недоступны");
            Check(Can(loop.Vm.StopCommand), "Running: Стоп доступен");
            await Task.Delay(50);
            loop.Vm.Stop();
            await run;
        }

        private static bool Can(ICommand command) => command.CanExecute(null);

        private static void Breakpoints()
        {
            var h = new Harness(SessionTests.Stepping);
            h.Vm.ToggleBreakpoint(5);
            h.Vm.ToggleBreakpoint(2);
            CheckEqual("2,5", string.Join(",", h.Vm.Breakpoints), "точки отсортированы");
            h.Vm.ToggleBreakpoint(5);
            CheckEqual("2", string.Join(",", h.Editor.Breakpoints), "повторный клик снимает точку");
            h.Vm.ToggleBreakpoint(0);
            h.Vm.ToggleBreakpoint(-4);
            CheckEqual(1, h.Vm.Breakpoints.Count, "строки ≤ 0 игнорируются");

            h.Editor.Caret = 7;
            h.Vm.ToggleBreakpointCommand.Execute(null);   // F9 — по строке курсора
            CheckEqual("2,7", string.Join(",", h.Vm.Breakpoints), "F9 ставит точку на строке курсора");
            h.Vm.ClearBreakpointsCommand.Execute(null);
            CheckEqual(0, h.Editor.Breakpoints.Count, "«Снять все» очистило и редактор");
        }

        private static async Task StaleSource()
        {
            var h = new Harness(SessionTests.Stepping);
            h.Vm.ToggleBreakpoint(8);
            await h.Vm.StartOrContinueAsync();
            Check(!h.Vm.IsSourceStale && !h.Vm.IsModified, "изначально код актуален");
            h.Vm.NotifyTextChanged();
            Check(h.Vm.IsSourceStale, "правка на паузе — подсветка может не совпадать с программой");
            Check(h.Vm.IsModified && h.Vm.WindowTitle.StartsWith("● ", StringComparison.Ordinal), "заголовок окна помечен");
            h.Vm.Stop();
            Check(!h.Vm.IsSourceStale, "закрытие сессии снимает пометку");
        }

        private static void Files()
        {
            string dir = Path.Combine(Path.GetTempPath(), "glass-studio-tests-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(dir);
            try
            {
                var h = new Harness("fn main() { print(1) }\n");
                h.Vm.NotifyTextChanged();
                Check(h.Vm.IsModified, "после правки — изменён");

                // Сохранить как…
                string path = Path.Combine(dir, "prog.glass");
                h.Dialogs.SavePath = path;
                Check(h.Vm.Save(), "сохранение прошло");
                CheckEqual("fn main() { print(1) }\n", File.ReadAllText(path), "файл содержит текст редактора");
                Check(!h.Vm.IsModified, "после сохранения — не изменён");
                CheckEqual(path, h.Vm.CurrentFilePath, "путь запомнен");
                CheckContains(h.Vm.WindowTitle, "prog.glass", "имя файла в заголовке окна");

                // Повторное сохранение — без диалога.
                h.Dialogs.SavePath = null;
                h.Editor.Text = "fn main() { print(2) }\n";
                h.Vm.NotifyTextChanged();
                Check(h.Vm.Save(), "повторное сохранение без диалога");
                CheckEqual("fn main() { print(2) }\n", File.ReadAllText(path), "перезаписан");

                // Сохранение отменено пользователем.
                var cancelled = new Harness("x");
                Check(!cancelled.Vm.Save(), "диалог отменён — не сохранено");

                // Открыть: есть несохранённые изменения, пользователь отказался их терять.
                File.WriteAllText(Path.Combine(dir, "other.glass"), "fn main() { print(3) }\n");
                h.Dialogs.OpenPath = Path.Combine(dir, "other.glass");
                h.Vm.NotifyTextChanged();
                h.Dialogs.Confirm = false;
                h.Vm.OpenFile();
                CheckEqual("fn main() { print(2) }\n", h.Editor.Text, "отказ — текст не тронут");
                CheckEqual(1, h.Dialogs.ConfirmCalls, "спросили о потере изменений");

                h.Dialogs.Confirm = true;
                h.Vm.OpenFile();
                CheckEqual("fn main() { print(3) }\n", h.Editor.Text, "согласие — файл открыт");
                Check(!h.Vm.IsModified, "после открытия — не изменён");
                CheckContains(h.Vm.WindowTitle, "other.glass", "в заголовке — новый файл");

                // Новый файл.
                h.Vm.NewFile();
                CheckEqual("", h.Editor.Text, "новый файл пуст");
                CheckEqual<string?>(null, h.Vm.CurrentFilePath, "пути нет");
                CheckContains(h.Vm.WindowTitle, "Без имени", "заголовок");

                // Выход: нет изменений — молча; есть — спрашиваем.
                int asked = h.Dialogs.ConfirmCalls;
                Check(h.Vm.ConfirmCanClose(), "без изменений закрыть можно");
                CheckEqual(asked, h.Dialogs.ConfirmCalls, "не спрашивали");
                h.Vm.NotifyTextChanged();
                h.Dialogs.Confirm = false;
                Check(!h.Vm.ConfirmCanClose(), "есть изменения и отказ — не закрываем");

                // Ошибка записи не роняет IDE.
                h.Dialogs.SavePath = Path.Combine(dir, "нет-такой-папки", "x.glass");
                Check(!h.Vm.SaveAs(), "запись в несуществующую папку — false");
                CheckContains(h.Vm.StatusText, "Не удалось сохранить", "и понятное сообщение");
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }
        }

        private static async Task ExamplesFlow()
        {
            foreach (ExampleItem item in ExampleCatalog.All)
            {
                var h = new Harness("");
                h.Vm.OpenExampleCommand.Execute(item);
                Check(h.Editor.Text.Length > 50, item.Id + ": текст загружен в редактор");
                Check(!h.Vm.IsModified, item.Id + ": пример не считается изменённым");
                CheckContains(h.Vm.WindowTitle, item.Title, item.Id + ": заголовок окна");
                await h.Vm.RunWithoutDebuggingAsync();
                CheckEqual(IdeState.Finished, h.Vm.State, item.Id + ": выполнился");
                Check(!h.Vm.HasProblems, item.Id + ": без ошибок");
                Check(h.Vm.Output.Length > 0, item.Id + ": есть вывод");
            }

            // Учебный сценарий «связный список»: цепочка видна на схеме.
            var list = new Harness("");
            list.Vm.OpenExample(ExampleCatalog.All.First(e => e.Id == "linked_list"));
            int line = list.Editor.Text.Split('\n').ToList().FindIndex(l => l.Trim() == "print(length(head))") + 1;   // не комментарий в шапке файла!
            Check(line > 0, "нашли строку вызова в примере");
            list.Vm.ToggleBreakpoint(line);
            await list.Vm.StartOrContinueAsync();
            CheckEqual(4, list.Vm.HeapGraph.Nodes.Count, "linked_list: четыре звена в куче");
            Check(list.Vm.HeapGraph.Nodes.Select(n => n.X).Distinct().Count() == 4, "они идут цепочкой — четыре колонки");
            CheckEqual(0, list.Vm.HeapGraph.GarbageCount, "мусора нет");

            // Учебный сценарий «сборка мусора».
            var gc = new Harness("");
            gc.Vm.OpenExample(ExampleCatalog.All.First(e => e.Id == "gc_demo"));
            int gcLine = gc.Editor.Text.Split('\n').ToList().FindIndex(l => l.Trim() == "~gc") + 1;
            gc.Vm.ToggleBreakpoint(gcLine);
            await gc.Vm.StartOrContinueAsync();
            CheckEqual(1, gc.Vm.HeapGraph.GarbageCount, "gc_demo: перед ~gc один объект уже мусор");
        }

        private static async Task LiveCheck()
        {
            var h = new Harness("fn main( {\n}\n");
            await h.Vm.CheckSourceAsync();
            CheckEqual(1, h.Vm.Problems.Count, "ошибка найдена без запуска");
            CheckEqual(1, h.Editor.Problems.Count, "и передана редактору");
            CheckEqual(IdeState.Editing, h.Vm.State, "состояние не изменилось");

            h.Editor.Text = SessionTests.Stepping;
            await h.Vm.CheckSourceAsync();
            Check(!h.Vm.HasProblems && h.Editor.Problems.Count == 0, "исправили — ошибки пропали");

            // Проверка не затирает результат выполнения: пока идёт программа, проблемы не трогаем.
            var loop = new Harness(SessionTests.Forever);
            Task run = loop.Vm.StartOrContinueAsync();
            loop.Editor.Text = "fn main( {\n}\n";
            await loop.Vm.CheckSourceAsync();
            Check(!loop.Vm.HasProblems, "во время выполнения проверка кода ничего не меняет");
            await Task.Delay(30);
            loop.Vm.Stop();
            await run;
        }

        private static async Task Restart()
        {
            var h = new Harness(SessionTests.Stepping);
            await h.Vm.RunWithoutDebuggingAsync();
            CheckEqual(IdeState.Finished, h.Vm.State, "первый запуск");
            CheckEqual("3", h.Vm.Output, "вывод первого запуска");

            // Изменили код и запустили снова: новая сессия с новым кодом, а не старая.
            h.Editor.Text = SessionTests.Stepping.Replace("add(1, 2)", "add(10, 20)");
            await h.Vm.RunWithoutDebuggingAsync();
            CheckEqual("30", h.Vm.Output, "вывод второго запуска — по новому коду");

            // F5 после завершения тоже начинает заново.
            h.Vm.ToggleBreakpoint(8);
            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Paused, h.Vm.State, "F5 после завершения — новая сессия с точкой останова");
            CheckEqual("", h.Vm.Output, "вывод сброшен");

            // Открытие примера посреди сессии закрывает её.
            h.Vm.OpenExample(ExampleCatalog.All[0]);
            CheckEqual(IdeState.Editing, h.Vm.State, "новый текст — сессия закрыта");
            CheckEqual(0, h.Vm.CallStack.Count, "стек очищен");
            CheckEqual(0, h.Vm.HeapGraph.Nodes.Count, "куча очищена");
            CheckEqual(0, h.Vm.Breakpoints.Count, "точки останова относились к прежнему коду — сняты");
            CheckEqual<int?>(null, h.Editor.ExecutionLine, "подсветка выполнения снята");
        }

        private static async Task HeapAfterFinish()
        {
            var h = new Harness(SessionTests.GcSource);
            h.Vm.ToggleBreakpoint(10);
            await h.Vm.StartOrContinueAsync();
            CheckEqual(1, h.Vm.HeapGraph.Nodes.Count, "на паузе — один живой объект (мусор уже собран ~gc)");
            await h.Vm.StartOrContinueAsync();
            CheckEqual(IdeState.Finished, h.Vm.State, "программа завершена");
            Check(h.Vm.IsHeapStale, "схема помечена как «до завершения»");
            CheckEqual(1, h.Vm.HeapGraph.Nodes.Count, "но не очищена — последнее состояние остаётся перед глазами");
        }

        private static async Task MenuAndBreakpointMoves()
        {
            var h = new Harness("");
            CheckEqual(ExampleCatalog.All.Count, h.Vm.ExampleMenu.Count, "в меню все примеры");
            ExampleMenuItem arrays = h.Vm.ExampleMenu.First(m => m.Example.Id == "arrays");
            Check(arrays.Command.CanExecute(null), "пункт меню доступен");
            arrays.Command.Execute(null);
            Check(h.Editor.Text.Contains("[", StringComparison.Ordinal) && h.Editor.Text.Length > 50, "пример загружен в редактор");
            CheckContains(h.Vm.WindowTitle, arrays.Title, "заголовок окна");

            // Пока программа выполняется, примеры недоступны — и пункты меню тоже.
            var loop = new Harness(SessionTests.Forever);
            Task run = loop.Vm.StartOrContinueAsync();
            Check(!loop.Vm.ExampleMenu[0].Command.CanExecute(null), "Running: пункты меню примеров недоступны");
            await Task.Delay(30);
            loop.Vm.Stop();
            await run;
            Check(loop.Vm.ExampleMenu[0].Command.CanExecute(null) == (loop.Vm.State != IdeState.Running), "после остановки доступность вернулась");

            // Редактор сдвинул точки останова после вставки строк.
            var b = new Harness(SessionTests.Stepping);
            b.Vm.ToggleBreakpoint(7);
            b.Vm.ReplaceBreakpoints(new[] { 9, 9, 0, -1 });
            CheckEqual("9", string.Join(",", b.Vm.Breakpoints), "набор заменён, мусор отброшен");
            CheckEqual("9", string.Join(",", b.Editor.Breakpoints), "редактор получил новый набор");
            b.Editor.Breakpoints.Clear();
            b.Vm.ReplaceBreakpoints(new[] { 9 });
            CheckEqual(0, b.Editor.Breakpoints.Count, "тот же набор — редактор заново не дёргаем");

            // Сдвинутая точка срабатывает на новой строке.
            b.Editor.Text = "\n\n" + SessionTests.Stepping;     // две пустые строки сверху: строка 8 стала 10
            b.Vm.ReplaceBreakpoints(new[] { 10 });
            await b.Vm.StartOrContinueAsync();
            CheckEqual(10, b.Editor.ExecutionLine, "остановились на сдвинутой строке");
        }

        private static async Task HeapSummary()
        {
            var h = new Harness(SessionTests.GcSource);
            CheckEqual("Куча пуста", h.Vm.HeapSummary, "до запуска");
            h.Vm.ToggleBreakpoint(9);
            await h.Vm.StartOrContinueAsync();
            CheckEqual("В куче: 2 объекта · мусор: 1", h.Vm.HeapSummary, "перед ~gc");
            h.Vm.CollectGarbage();
            CheckEqual("В куче: 1 объект", h.Vm.HeapSummary, "после сборки");
            h.Vm.Stop();
            CheckEqual("Куча пуста", h.Vm.HeapSummary, "после закрытия сессии");
        }
    }
}
