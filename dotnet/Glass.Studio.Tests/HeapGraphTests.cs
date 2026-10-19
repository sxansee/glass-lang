using System;
using System.Collections.Generic;
using System.Linq;
using Glass.Studio.Heap;
using Glass.Studio.Models;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>Раскладка графа кучи — чистая функция, проверяется на синтетических снимках.</summary>
    public static class HeapGraphTests
    {
        public static void Register()
        {
            Add("граф: цепочка — колонки слева направо, рёбра, корень", Chain);
            Add("граф: общий объект и порядок слоёв (длиннейший путь)", SharedAndLayering);
            Add("граф: цикл и ссылка на себя не зацикливают раскладку", Cycles);
            Add("граф: мусор — отдельная колонка, достижимость от VM", Garbage);
            Add("граф: новое и изменённое с прошлой паузы", Diff);
            Add("граф: лимиты — много узлов, длинный массив", Limits);
            Add("граф: выделение узла подсвечивает его рёбра", Selection);
            Add("граф: геометрия — узлы не перекрываются и лежат внутри холста", Geometry);
            Add("граф: длинный список (200 звеньев) — быстро и без переполнения стека", LongList);
        }

        private static GlassValue Ref(string address, string cls = "Node") => GlassValue.ObjectRef(address, cls);

        private static HeapObject Node(string address, long value, GlassValue next, bool reachable = true) =>
            new HeapObject(address, false, "Node", reachable, new[]
            {
                new LocalVariable("value", GlassValue.FromInt(value)),
                new LocalVariable("next", next),
            });

        private static FrameLocals Frame(params (string Name, GlassValue Value)[] locals) =>
            new FrameLocals(0, "main", 10, locals.Select(l => new LocalVariable(l.Name, l.Value)).ToList());

        private static void Chain()
        {
            var heap = new HeapSnapshot(new[]
            {
                Node("1", 10, Ref("2")),
                Node("2", 20, Ref("3")),
                Node("3", 30, GlassValue.Null),
            });
            HeapGraphModel g = HeapGraphBuilder.Build(heap, new[] { Frame(("head", Ref("1"))) }, null, null);

            CheckEqual(3, g.Nodes.Count, "три узла");
            CheckEqual(1, g.Roots.Count, "один корень — переменная head");
            CheckEqual("main · head", g.Roots[0].Label, "подпись корня");
            CheckEqual(3, g.Edges.Count, "рёбра: корень→1, 1→2, 2→3");
            CheckEqual(1, g.Edges.Count(e => e.IsFromRoot), "одно ребро от корня");
            Check(g.Nodes[0].X < g.Nodes[1].X && g.Nodes[1].X < g.Nodes[2].X, "узлы идут слева направо по цепочке");
            Check(g.Roots[0].X + g.Roots[0].Width < g.Nodes[0].X, "корни левее узлов");
            Check(g.Edges.All(e => e.PathData.StartsWith("M ", StringComparison.Ordinal) && e.PathData.Contains(" C ")), "у всех рёбер есть геометрия");
            CheckEqual(0, g.GarbageCount, "мусора нет");
            CheckEqual("value", g.Nodes[0].Rows[0].Label, "первая строка узла — поле");
            CheckEqual("10", g.Nodes[0].Rows[0].ValueText, "значение поля");
            Check(g.Nodes[0].Rows[1].IsReference, "next — ссылка");
            CheckEqual("null", g.Nodes[2].Rows[1].ValueText, "null виден как null");
        }

        private static void SharedAndLayering()
        {
            // a и b — две локали; b.next → a. Корни указывают на оба, но a — ПРЕЕМНИК b,
            // поэтому лежит правее (слой 1), а не рядом.
            var heap = new HeapSnapshot(new[]
            {
                Node("1", 1, GlassValue.Null),   // a
                Node("2", 2, Ref("1")),           // b
            });
            HeapGraphModel g = HeapGraphBuilder.Build(heap, new[] { Frame(("a", Ref("1")), ("b", Ref("2"))) }, null, null);
            HeapNodeModel a = g.FindNode("1")!;
            HeapNodeModel b = g.FindNode("2")!;
            Check(b.X < a.X, "b (держит ссылку) левее a");
            CheckEqual(2, g.Roots.Count, "два корня");
            CheckEqual(3, g.Edges.Count, "корень→a, корень→b, b→a");

            // Один объект, на который два корня: один узел, два ребра от корней.
            var one = new HeapSnapshot(new[] { Node("1", 1, GlassValue.Null) });
            HeapGraphModel shared = HeapGraphBuilder.Build(one, new[] { Frame(("p", Ref("1")), ("q", Ref("1"))) }, null, null);
            CheckEqual(1, shared.Nodes.Count, "общий объект — один узел");
            CheckEqual(2, shared.Edges.Count(e => e.IsFromRoot), "два корня — два ребра");
        }

        private static void Cycles()
        {
            var heap = new HeapSnapshot(new[]
            {
                Node("1", 1, Ref("2")),
                Node("2", 2, Ref("1")),   // цикл 1 → 2 → 1
                Node("3", 3, Ref("3")),   // ссылка на себя
            });
            HeapGraphModel g = HeapGraphBuilder.Build(heap, new[] { Frame(("x", Ref("1")), ("y", Ref("3"))) }, null, null);
            CheckEqual(3, g.Nodes.Count, "все три узла нарисованы");
            Check(g.FindNode("1")!.X < g.FindNode("2")!.X, "в цикле порядок слоёв определяется первым ребром");
            CheckEqual(2 + 1 + 1 + 1, g.Edges.Count, "рёбра: 2 от корней, 1→2, 2→1, 3→3");
            Check(g.Edges.All(e => !e.PathData.Contains("NaN") && !e.PathData.Contains("∞")), "геометрия без NaN/бесконечностей");
            Check(g.Width > 0 && g.Height > 0, "холст имеет размер");
        }

        private static void Garbage()
        {
            var heap = new HeapSnapshot(new[]
            {
                Node("1", 1, GlassValue.Null),
                Node("9", 9, Ref("1"), reachable: false),   // никто не ссылается, VM пометила недостижимым
            });
            HeapGraphModel g = HeapGraphBuilder.Build(heap, new[] { Frame(("keep", Ref("1"))) }, null, null);
            CheckEqual(1, g.GarbageCount, "один мусорный объект");
            HeapNodeModel garbage = g.FindNode("9")!;
            Check(garbage.IsGarbage && !garbage.IsReachable, "объект помечен мусором");
            Check(!g.FindNode("1")!.IsGarbage, "живой — не мусор");
            Check(garbage.X > g.FindNode("1")!.X, "мусор — в отдельной колонке справа");
            Check(g.Edges.Any(e => e.IsFromGarbage && e.FromAddress == "9" && e.ToAddress == "1"), "ссылка мусора на живой объект видна");

            // VM говорит «достижим», хотя от видимых локалей не дойти (временное значение/глубокий фрейм).
            var hidden = new HeapSnapshot(new[] { Node("5", 5, GlassValue.Null, reachable: true) });
            HeapGraphModel temp = HeapGraphBuilder.Build(hidden, Array.Empty<FrameLocals>(), null, null);
            CheckEqual(0, temp.GarbageCount, "достижимое по мнению VM — не мусор");
            CheckEqual(1, temp.Nodes.Count, "и оно нарисовано");
        }

        private static void Diff()
        {
            var before = new HeapSnapshot(new[] { Node("1", 1, GlassValue.Null), Node("2", 2, GlassValue.Null) });
            var after = new HeapSnapshot(new[]
            {
                Node("1", 1, GlassValue.Null),      // не менялся
                Node("2", 99, GlassValue.Null),     // изменилось поле value
                Node("3", 3, GlassValue.Null),      // новый
            });
            var frames = new[] { Frame(("a", Ref("1")), ("b", Ref("2")), ("c", Ref("3"))) };

            HeapGraphModel first = HeapGraphBuilder.Build(before, frames, null, null);
            Check(first.Nodes.All(n => n.Change == ChangeState.None), "без прошлой паузы ничего не подсвечиваем");

            HeapGraphModel g = HeapGraphBuilder.Build(after, frames, before, null);
            CheckEqual(ChangeState.None, g.FindNode("1")!.Change, "прежний узел — без изменений");
            CheckEqual(ChangeState.Changed, g.FindNode("2")!.Change, "изменённое поле");
            CheckEqual(ChangeState.New, g.FindNode("3")!.Change, "новый объект");
            Check(g.FindNode("2")!.Rows[0].Changed && !g.FindNode("2")!.Rows[1].Changed, "подсвечена именно изменённая строка");
            Check(g.FindNode("2")!.IsChanged && !g.FindNode("2")!.IsNew, "флаги для привязок: изменён, не новый");
            Check(g.FindNode("3")!.IsNew && !g.FindNode("3")!.IsChanged, "флаги для привязок: новый, не изменён");
        }

        private static void Limits()
        {
            // Массив из 30 элементов — строк больше предела: «… ещё N».
            var elements = Enumerable.Range(0, 30).Select(i => new LocalVariable("[" + i + "]", GlassValue.FromInt(i))).ToList();
            var arr = new HeapSnapshot(new[] { new HeapObject("1", true, "[]", true, elements) });
            HeapGraphModel g = HeapGraphBuilder.Build(arr, new[] { Frame(("xs", GlassValue.ArrayRef("1", 30))) }, null, null);
            HeapNodeModel node = g.Nodes[0];
            CheckEqual(HeapMetrics.MaxRowsPerNode, node.Rows.Count, "строк не больше предела");
            CheckEqual("… ещё " + (30 - HeapMetrics.MaxRowsPerNode), node.OverflowText, "строка-остаток");
            Check(node.Height > HeapMetrics.HeaderHeight + HeapMetrics.MaxRowsPerNode * HeapMetrics.RowHeight, "высота учитывает строку-остаток");

            // Больше узлов, чем можно нарисовать.
            int total = HeapMetrics.MaxNodes + 40;
            var many = new HeapSnapshot(Enumerable.Range(1, total).Select(i => Node(i.ToString(), i, GlassValue.Null)).ToList());
            var rootsLocals = Enumerable.Range(1, total).Select(i => ("v" + i, Ref(i.ToString()))).ToArray();
            HeapGraphModel big = HeapGraphBuilder.Build(many, new[] { Frame(rootsLocals) }, null, null);
            CheckEqual(HeapMetrics.MaxNodes, big.Nodes.Count, "нарисовано не больше MaxNodes");
            CheckEqual(40, big.HiddenNodeCount, "остальные посчитаны как скрытые");
        }

        private static void Selection()
        {
            var heap = new HeapSnapshot(new[] { Node("1", 1, Ref("2")), Node("2", 2, GlassValue.Null), Node("3", 3, GlassValue.Null) });
            var frames = new[] { Frame(("a", Ref("1")), ("c", Ref("3"))) };
            HeapGraphModel g = HeapGraphBuilder.Build(heap, frames, null, "2");
            Check(g.FindNode("2")!.IsSelected, "выбор пережил перестроение графа");
            Check(g.Edges.Where(e => e.ToAddress == "2").All(e => e.Emphasized), "рёбра к выбранному — ярче");
            Check(g.Edges.Where(e => e.ToAddress != "2" && e.FromAddress != "2").All(e => !e.Emphasized), "остальные — обычные");

            g.Select("3");
            Check(g.FindNode("3")!.IsSelected && !g.FindNode("2")!.IsSelected, "выбор переключается");
            g.Select(null);
            Check(g.Nodes.All(n => !n.IsSelected) && g.Edges.All(e => !e.Emphasized), "null снимает выделение");
            Check(g.FindNode("нет такого") == null, "поиск несуществующего — null");
        }

        private static void Geometry()
        {
            // Разветвлённый граф: дерево + общий лист + мусор.
            var heap = new HeapSnapshot(new[]
            {
                new HeapObject("1", false, "Tree", true, new[]
                {
                    new LocalVariable("left", Ref("2", "Tree")), new LocalVariable("right", Ref("3", "Tree")),
                }),
                new HeapObject("2", false, "Tree", true, new[]
                {
                    new LocalVariable("left", Ref("4", "Leaf")), new LocalVariable("right", Ref("4", "Leaf")),
                }),
                new HeapObject("3", false, "Tree", true, new[]
                {
                    new LocalVariable("left", Ref("4", "Leaf")), new LocalVariable("right", GlassValue.Null),
                }),
                new HeapObject("4", false, "Leaf", true, new[] { new LocalVariable("name", GlassValue.FromString("общий лист с длинным именем")) }),
                new HeapObject("5", false, "Orphan", false, new[] { new LocalVariable("to", Ref("4", "Leaf")) }),
            });
            HeapGraphModel g = HeapGraphBuilder.Build(heap, new[] { Frame(("root", Ref("1", "Tree"))) }, null, null);

            CheckEqual(5, g.Nodes.Count, "пять узлов");
            for (int i = 0; i < g.Nodes.Count; i++)
            {
                HeapNodeModel a = g.Nodes[i];
                Check(a.X >= 0 && a.Y >= 0 && a.X + a.Width <= g.Width && a.Y + a.Height <= g.Height, "узел " + a.Title + " внутри холста");
                for (int j = i + 1; j < g.Nodes.Count; j++)
                {
                    HeapNodeModel b = g.Nodes[j];
                    bool overlap = a.X < b.X + b.Width && b.X < a.X + a.Width && a.Y < b.Y + b.Height && b.Y < a.Y + a.Height;
                    Check(!overlap, "узлы " + a.Title + a.Address + " и " + b.Title + b.Address + " не перекрываются");
                }
            }
            foreach (RootModel r in g.Roots) Check(r.X >= 0 && r.Y >= 0 && r.Y + r.Height <= g.Height, "корень внутри холста");
            // Одинаковые ссылки из одной строки не дублируются, из разных строк — остаются (2→4 дважды из left и right).
            CheckEqual(2, g.Edges.Count(e => e.FromAddress == "2" && e.ToAddress == "4"), "две разные строки — два ребра");
        }

        private static void LongList()
        {
            const int length = 3000;   // в снимке много, рисуется MaxNodes
            var objects = new List<HeapObject>(length);
            for (int i = 1; i <= length; i++) objects.Add(Node(i.ToString(), i, i < length ? Ref((i + 1).ToString()) : GlassValue.Null));
            var snapshot = new HeapSnapshot(objects);

            var watch = System.Diagnostics.Stopwatch.StartNew();
            HeapGraphModel g = HeapGraphBuilder.Build(snapshot, new[] { Frame(("head", Ref("1"))) }, null, null);
            watch.Stop();

            CheckEqual(HeapMetrics.MaxNodes, g.Nodes.Count, "нарисовано MaxNodes звеньев");
            CheckEqual(length - HeapMetrics.MaxNodes, g.HiddenNodeCount, "остальные скрыты");
            Check(g.Nodes[0].X < g.Nodes[HeapMetrics.MaxNodes - 1].X, "цепочка идёт вправо");
            Check(watch.ElapsedMilliseconds < 2000, "построение уложилось в 2 с (заняло " + watch.ElapsedMilliseconds + " мс)");
        }
    }
}
