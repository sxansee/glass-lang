// Граф кучи для визуализации: узлы (объекты/массивы), рёбра (ссылки из
// полей/элементов), корни (ссылочные локальные переменные всех фреймов) —
// вместе с ГОТОВОЙ раскладкой в координатах. Раскладка считается здесь, а не
// в XAML, по двум причинам: (1) её можно проверить тестами без Windows;
// (2) рёбра и порты строк обязаны попадать ровно в строки узлов — XAML
// рисует узлы по тем же константам (HeapMetrics), через x:Static.
//
// Достижимость — НЕ догадка UI: флаг Reachable приходит из VM (пробный
// обход от всех корней GC, включая временные значения), поэтому "мусор" на
// схеме — ровно то, что освободит следующая сборка.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Glass.Studio.Models;
using Glass.Studio.Mvvm;

namespace Glass.Studio.Heap
{
    public enum ChangeState { None, New, Changed }

    /// <summary>Размеры, общие для раскладки (C#) и шаблонов (XAML, через x:Static).</summary>
    public static class HeapMetrics
    {
        public const double HeaderHeight = 34;
        public const double RowHeight = 22;
        public const double BottomPadding = 6;
        public const double MinNodeWidth = 150;
        public const double CharWidth = 7.4;          // Consolas 12px, с запасом
        public const double HorizontalPadding = 12;
        public const double ValueGap = 18;
        public const double PortSpace = 16;
        public const double ColumnGap = 96;
        public const double VerticalGap = 18;
        public const double RootChipWidth = 190;
        public const double RootChipHeight = 28;
        public const double RootGap = 8;
        public const double FrameGap = 12;
        public const double Margin = 16;
        public const int MaxRowsPerNode = 12;
        public const int MaxNodes = 200;
    }

    public sealed class HeapRowModel
    {
        public HeapRowModel(string label, string valueText, bool isReference, bool changed)
        {
            Label = label;
            ValueText = valueText;
            IsReference = isReference;
            Changed = changed;
        }

        public string Label { get; }
        public string ValueText { get; }
        public bool IsReference { get; }
        /// <summary>Значение изменилось с предыдущей паузы.</summary>
        public bool Changed { get; }
    }

    public sealed class HeapNodeModel : ObservableObject
    {
        private bool _isSelected;

        public HeapNodeModel(string address, string title, string subtitle, bool isArray, bool isReachable,
                             ChangeState change, IReadOnlyList<HeapRowModel> rows, string? overflowText)
        {
            Address = address;
            Title = title;
            Subtitle = subtitle;
            IsArray = isArray;
            IsReachable = isReachable;
            Change = change;
            Rows = rows;
            OverflowText = overflowText;
        }

        /// <summary>Десятичная строка, как в JSON; по ней же узлы ищутся и связываются.</summary>
        public string Address { get; }
        public string Title { get; }
        public string Subtitle { get; }
        public bool IsArray { get; }
        /// <summary>false — на объект не ссылается ничто от корней GC: следующая сборка его освободит.</summary>
        public bool IsReachable { get; }
        public bool IsGarbage => !IsReachable;
        public ChangeState Change { get; }
        public bool IsNew => Change == ChangeState.New;
        public bool IsChanged => Change == ChangeState.Changed;
        public IReadOnlyList<HeapRowModel> Rows { get; }
        /// <summary>"… ещё N" — если у узла больше строк, чем помещается (длинные массивы).</summary>
        public string? OverflowText { get; }
        public bool HasOverflow => OverflowText != null;

        public double X { get; internal set; }
        public double Y { get; internal set; }
        public double Width { get; internal set; }
        public double Height { get; internal set; }

        public bool IsSelected
        {
            get => _isSelected;
            set => SetProperty(ref _isSelected, value);
        }
    }

    public sealed class HeapEdgeModel : ObservableObject
    {
        private bool _emphasized;

        public HeapEdgeModel(string? fromAddress, string toAddress, string pathData, bool fromRoot, bool fromGarbage)
        {
            FromAddress = fromAddress;
            ToAddress = toAddress;
            PathData = pathData;
            IsFromRoot = fromRoot;
            IsFromGarbage = fromGarbage;
        }

        /// <summary>null — ребро идёт от корня (локальной переменной), а не от объекта кучи.</summary>
        public string? FromAddress { get; }
        public string ToAddress { get; }
        /// <summary>Геометрия в синтаксисе XAML (Path.Data): кривая Безье + стрелка.</summary>
        public string PathData { get; }
        public bool IsFromRoot { get; }
        public bool IsFromGarbage { get; }

        /// <summary>Ребро касается выбранного узла — рисуется ярче.</summary>
        public bool Emphasized
        {
            get => _emphasized;
            set => SetProperty(ref _emphasized, value);
        }
    }

    /// <summary>Ссылочная локальная переменная — «корень», от которого начинается достижимость.</summary>
    public sealed class RootModel
    {
        public RootModel(string label, string frameName, string targetAddress, double x, double y)
        {
            Label = label;
            FrameName = frameName;
            TargetAddress = targetAddress;
            X = x;
            Y = y;
        }

        public string Label { get; }
        public string FrameName { get; }
        public string TargetAddress { get; }
        public double X { get; }
        public double Y { get; }
        public double Width => HeapMetrics.RootChipWidth;
        public double Height => HeapMetrics.RootChipHeight;
    }

    public sealed class HeapGraphModel
    {
        public static readonly HeapGraphModel Empty = new HeapGraphModel(
            Array.Empty<HeapNodeModel>(), Array.Empty<HeapEdgeModel>(), Array.Empty<RootModel>(), 0, 0, 0, 0);

        public HeapGraphModel(IReadOnlyList<HeapNodeModel> nodes, IReadOnlyList<HeapEdgeModel> edges,
                              IReadOnlyList<RootModel> roots, double width, double height,
                              int hiddenNodeCount, int garbageCount)
        {
            Nodes = nodes;
            Edges = edges;
            Roots = roots;
            Width = width;
            Height = height;
            HiddenNodeCount = hiddenNodeCount;
            GarbageCount = garbageCount;
        }

        public IReadOnlyList<HeapNodeModel> Nodes { get; }
        public IReadOnlyList<HeapEdgeModel> Edges { get; }
        public IReadOnlyList<RootModel> Roots { get; }
        public double Width { get; }
        public double Height { get; }
        /// <summary>Сколько объектов не нарисовано из-за ограничения MaxNodes.</summary>
        public int HiddenNodeCount { get; }
        /// <summary>Сколько объектов недостижимы (мусор) среди нарисованных.</summary>
        public int GarbageCount { get; }
        public bool IsEmpty => Nodes.Count == 0 && Roots.Count == 0;

        public HeapNodeModel? FindNode(string? address) =>
            address == null ? null : Nodes.FirstOrDefault(n => n.Address == address);

        /// <summary>Подсвечивает узел и касающиеся его рёбра; null — снять выделение.</summary>
        public void Select(string? address)
        {
            foreach (HeapNodeModel node in Nodes) node.IsSelected = address != null && node.Address == address;
            foreach (HeapEdgeModel edge in Edges)
            {
                edge.Emphasized = address != null && (edge.FromAddress == address || edge.ToAddress == address);
            }
        }
    }

    public static class HeapGraphBuilder
    {
        /// <param name="snapshot">Снимок кучи на текущей паузе.</param>
        /// <param name="frames">Локальные переменные ВСЕХ фреймов — источник корней.</param>
        /// <param name="previous">Снимок прошлой паузы (для подсветки нового/изменённого) или null.</param>
        /// <param name="selectedAddress">Выбранный узел, чтобы выделение пережило перестроение.</param>
        public static HeapGraphModel Build(HeapSnapshot snapshot, IReadOnlyList<FrameLocals> frames,
                                           HeapSnapshot? previous, string? selectedAddress)
        {
            var byAddress = new Dictionary<string, HeapObject>();
            foreach (HeapObject o in snapshot.Objects) byAddress.TryAdd(o.Address, o);

            Dictionary<string, HeapObject>? previousByAddress = null;
            if (previous != null)
            {
                previousByAddress = new Dictionary<string, HeapObject>();
                foreach (HeapObject o in previous.Objects) previousByAddress.TryAdd(o.Address, o);
            }

            // --- корни: ссылочные локали всех фреймов ---
            var roots = new List<(string Label, string Frame, string Target, int FrameDepth)>();
            foreach (FrameLocals frame in frames)
            {
                foreach (LocalVariable local in frame.Locals)
                {
                    if (local.Value.IsReference && byAddress.ContainsKey(local.Value.Address!))
                    {
                        roots.Add((frame.Function + " · " + local.Name, frame.Function, local.Value.Address!, frame.Depth));
                    }
                }
            }

            // --- обход от корней (BFS): какие объекты достижимы и в каком порядке их встретили.
            //     Объекты, которые VM считает достижимыми, но от видимых локалей до них не дойти
            //     (их держит временное значение недовычисленного выражения или фрейм глубже
            //     MaxFramesWithLocals), тоже запускают обход — рисуем их как достижимые. ---
            var order = new List<string>();
            var discovered = new HashSet<string>();
            var queue = new Queue<string>();

            void Visit(string start)
            {
                if (!discovered.Add(start)) return;
                order.Add(start);
                queue.Enqueue(start);
                while (queue.Count > 0)
                {
                    string current = queue.Dequeue();
                    foreach (LocalVariable member in byAddress[current].Members)
                    {
                        if (!member.Value.IsReference) continue;
                        string target = member.Value.Address!;
                        if (byAddress.ContainsKey(target) && discovered.Add(target))
                        {
                            order.Add(target);
                            queue.Enqueue(target);
                        }
                    }
                }
            }

            foreach (var root in roots) Visit(root.Target);
            foreach (HeapObject o in snapshot.Objects)
            {
                if (o.Reachable && !discovered.Contains(o.Address)) Visit(o.Address);
            }

            var garbage = snapshot.Objects.Where(o => !discovered.Contains(o.Address)).Select(o => o.Address).Distinct().ToList();

            // --- какие узлы рисуем (лимит): достижимые по порядку обхода, затем мусор ---
            var shownOrder = new List<string>();
            foreach (string a in order) { if (shownOrder.Count < HeapMetrics.MaxNodes) shownOrder.Add(a); }
            var shownGarbage = new List<string>();
            foreach (string a in garbage)
            {
                if (shownOrder.Count + shownGarbage.Count < HeapMetrics.MaxNodes) shownGarbage.Add(a);
            }
            int hidden = order.Count + garbage.Count - shownOrder.Count - shownGarbage.Count;

            // --- слои: самый длинный путь от корней по рёбрам «объект → объект»; обратные рёбра
            //     (циклы) в расчёт слоёв не входят — иначе цикл растягивался бы бесконечно ---
            Dictionary<string, int> layerOf = AssignLayers(shownOrder, roots.Select(r => r.Target).ToList(), byAddress);

            // --- модели узлов ---
            var nodeByAddress = new Dictionary<string, HeapNodeModel>();
            foreach (string address in shownOrder.Concat(shownGarbage))
            {
                HeapObject obj = byAddress[address];
                HeapObject? prev = null;
                bool hasPrevious = previousByAddress != null;  // null — прошлой паузы нет, подсветку не считаем
                if (hasPrevious) previousByAddress!.TryGetValue(address, out prev);
                nodeByAddress[address] = CreateNode(obj, discovered.Contains(address), hasPrevious, prev);
            }

            // --- колонки и координаты (внутри слоя — в порядке обнаружения) ---
            double colStart = HeapMetrics.Margin + (roots.Count > 0 ? HeapMetrics.RootChipWidth + HeapMetrics.ColumnGap : 0);
            var layers = new SortedDictionary<int, List<HeapNodeModel>>();
            foreach (string address in shownOrder)
            {
                int layer = layerOf[address];
                if (!layers.TryGetValue(layer, out List<HeapNodeModel>? list)) layers[layer] = list = new List<HeapNodeModel>();
                list.Add(nodeByAddress[address]);
            }
            double x = colStart;
            double maxRight = colStart;
            double maxBottom = HeapMetrics.Margin;
            foreach (List<HeapNodeModel> column in layers.Values)
            {
                double y = HeapMetrics.Margin;
                double columnWidth = column.Max(n => n.Width);
                foreach (HeapNodeModel node in column)
                {
                    node.X = x;
                    node.Y = y;
                    y += node.Height + HeapMetrics.VerticalGap;
                }
                maxBottom = Math.Max(maxBottom, y - HeapMetrics.VerticalGap);
                maxRight = Math.Max(maxRight, x + columnWidth);
                x += columnWidth + HeapMetrics.ColumnGap;
            }
            if (shownGarbage.Count > 0)
            {
                double y = HeapMetrics.Margin;
                double columnWidth = shownGarbage.Max(a => nodeByAddress[a].Width);
                foreach (string address in shownGarbage)
                {
                    HeapNodeModel node = nodeByAddress[address];
                    node.X = x;
                    node.Y = y;
                    y += node.Height + HeapMetrics.VerticalGap;
                }
                maxBottom = Math.Max(maxBottom, y - HeapMetrics.VerticalGap);
                maxRight = Math.Max(maxRight, x + columnWidth);
            }

            // --- корни (колонка слева) ---
            var rootModels = new List<RootModel>();
            double ry = HeapMetrics.Margin;
            int lastDepth = int.MinValue;
            foreach (var root in roots)
            {
                if (lastDepth != int.MinValue && root.FrameDepth != lastDepth) ry += HeapMetrics.FrameGap;
                lastDepth = root.FrameDepth;
                rootModels.Add(new RootModel(root.Label, root.Frame, root.Target, HeapMetrics.Margin, ry));
                ry += HeapMetrics.RootChipHeight + HeapMetrics.RootGap;
            }
            if (rootModels.Count > 0) maxBottom = Math.Max(maxBottom, ry - HeapMetrics.RootGap);

            // --- рёбра ---
            var edges = new List<HeapEdgeModel>();
            foreach (RootModel root in rootModels)
            {
                if (!nodeByAddress.TryGetValue(root.TargetAddress, out HeapNodeModel? target)) continue;
                edges.Add(new HeapEdgeModel(null, target.Address,
                    ForwardPath(root.X + root.Width, root.Y + root.Height / 2, target), fromRoot: true, fromGarbage: false));
            }
            foreach (string address in shownOrder.Concat(shownGarbage))
            {
                HeapNodeModel source = nodeByAddress[address];
                HeapObject obj = byAddress[address];
                int visibleRows = source.Rows.Count;
                var seen = new HashSet<(int Row, string Target)>();
                for (int i = 0; i < obj.Members.Count; i++)
                {
                    GlassValue value = obj.Members[i].Value;
                    if (!value.IsReference || !nodeByAddress.TryGetValue(value.Address!, out HeapNodeModel? target)) continue;
                    int row = Math.Min(i, visibleRows);  // скрытые элементы выходят из строки «… ещё N»
                    if (!seen.Add((row, target.Address))) continue;
                    double rowY = source.Y + HeapMetrics.HeaderHeight + row * HeapMetrics.RowHeight + HeapMetrics.RowHeight / 2;
                    edges.Add(new HeapEdgeModel(source.Address, target.Address,
                        ObjectEdgePath(source, rowY, target),
                        fromRoot: false, fromGarbage: !discovered.Contains(address)));
                }
            }

            var model = new HeapGraphModel(
                shownOrder.Concat(shownGarbage).Select(a => nodeByAddress[a]).ToList(),
                edges, rootModels,
                maxRight + HeapMetrics.Margin, maxBottom + HeapMetrics.Margin,
                hidden, shownGarbage.Count);
            model.Select(selectedAddress);
            return model;
        }

        /// <summary>
        /// Слой узла = длина самого длинного пути до него от стартовых узлов по прямым рёбрам.
        /// Обход в глубину итеративный (длинный список — тысячи звеньев — не должен переполнить стек);
        /// ребро в узел, который «ещё в обработке», — обратное (замыкает цикл) и слои не двигает.
        /// </summary>
        private static Dictionary<string, int> AssignLayers(List<string> shown, List<string> rootTargets,
                                                           Dictionary<string, HeapObject> byAddress)
        {
            var shownSet = new HashSet<string>(shown);
            var successors = new Dictionary<string, List<string>>();
            foreach (string address in shown)
            {
                var list = new List<string>();
                var seen = new HashSet<string>();
                foreach (LocalVariable member in byAddress[address].Members)
                {
                    if (!member.Value.IsReference) continue;
                    string target = member.Value.Address!;
                    if (target != address && shownSet.Contains(target) && seen.Add(target)) list.Add(target);
                }
                successors[address] = list;
            }

            var state = new Dictionary<string, int>();   // 1 — в обработке, 2 — готов
            var postOrder = new List<string>();
            var backEdges = new HashSet<(string, string)>();
            var starts = rootTargets.Where(shownSet.Contains).Concat(shown);
            foreach (string start in starts)
            {
                if (state.ContainsKey(start)) continue;
                var stack = new Stack<(string Node, int Next)>();
                state[start] = 1;
                stack.Push((start, 0));
                while (stack.Count > 0)
                {
                    (string node, int next) = stack.Pop();
                    List<string> children = successors[node];
                    if (next < children.Count)
                    {
                        stack.Push((node, next + 1));
                        string child = children[next];
                        if (!state.TryGetValue(child, out int childState))
                        {
                            state[child] = 1;
                            stack.Push((child, 0));
                        }
                        else if (childState == 1)
                        {
                            backEdges.Add((node, child));
                        }
                    }
                    else
                    {
                        state[node] = 2;
                        postOrder.Add(node);
                    }
                }
            }

            var layer = new Dictionary<string, int>();
            foreach (string address in shown) layer[address] = 0;
            for (int i = postOrder.Count - 1; i >= 0; i--)   // обратный постпорядок — топологический
            {
                string node = postOrder[i];
                foreach (string child in successors[node])
                {
                    if (backEdges.Contains((node, child))) continue;
                    if (layer[child] < layer[node] + 1) layer[child] = layer[node] + 1;
                }
            }
            return layer;
        }

        private static HeapNodeModel CreateNode(HeapObject obj, bool reachableFromRoots, bool hasPrevious, HeapObject? previousObject)
        {
            int visible = Math.Min(obj.Members.Count, HeapMetrics.MaxRowsPerNode);
            var rows = new List<HeapRowModel>(visible);
            bool anyChanged = false;
            for (int i = 0; i < obj.Members.Count; i++)
            {
                LocalVariable member = obj.Members[i];
                bool changed = hasPrevious && previousObject != null &&
                               (i >= previousObject.Members.Count || !SameValue(previousObject.Members[i].Value, member.Value));
                anyChanged |= changed;
                if (i < visible)
                {
                    string text = member.Value.IsReference ? "→ " + member.Value.Display : member.Value.Display;
                    rows.Add(new HeapRowModel(member.Name, text, member.Value.IsReference, changed));
                }
            }
            string? overflow = obj.Members.Count > visible
                ? "… ещё " + (obj.Members.Count - visible).ToString(CultureInfo.InvariantCulture)
                : null;

            ChangeState change = !hasPrevious ? ChangeState.None
                : previousObject == null ? ChangeState.New
                : anyChanged ? ChangeState.Changed : ChangeState.None;

            string title = obj.IsArray ? "array" : obj.ClassName;
            string shortAddress = "@" + AddressFormat.Short(obj.Address);
            string subtitle = obj.IsArray
                ? "[" + obj.Members.Count.ToString(CultureInfo.InvariantCulture) + "] " + shortAddress
                : shortAddress;

            // Мусором считаем и то, что VM пометила недостижимым, и то, до чего не дошёл обход.
            bool reachable = obj.Reachable || reachableFromRoots;
            var node = new HeapNodeModel(obj.Address, title, subtitle, obj.IsArray, reachable, change, rows, overflow);

            int labelChars = rows.Count == 0 ? 0 : rows.Max(r => r.Label.Length);
            int valueChars = rows.Count == 0 ? 0 : rows.Max(r => r.ValueText.Length);
            double bodyWidth = 2 * HeapMetrics.HorizontalPadding + (labelChars + valueChars) * HeapMetrics.CharWidth +
                               HeapMetrics.ValueGap + HeapMetrics.PortSpace;
            double headerWidth = 2 * HeapMetrics.HorizontalPadding + (title.Length + subtitle.Length) * HeapMetrics.CharWidth + 16;
            node.Width = Math.Ceiling(Math.Max(HeapMetrics.MinNodeWidth, Math.Max(bodyWidth, headerWidth)));
            node.Height = HeapMetrics.HeaderHeight + (rows.Count + (overflow != null ? 1 : 0)) * HeapMetrics.RowHeight +
                          HeapMetrics.BottomPadding;
            return node;
        }

        private static bool SameValue(GlassValue a, GlassValue b) =>
            a.Kind == b.Kind && a.Display == b.Display && a.Address == b.Address;

        /// <summary>
        /// Ребро от строки узла-источника к заголовку цели. Три случая, чтобы линия не
        /// пересекала собственный узел и не тянулась через всю схему:
        ///  · цель правее (обычный случай, BFS-слои идут слева направо) — вперёд;
        ///  · цель строго левее (ссылка из мусорного узла на живой, обратная ссылка) —
        ///    от ЛЕВОГО края источника к ПРАВОМУ краю цели;
        ///  · та же колонка или сам на себя — петля справа.
        /// </summary>
        private static string ObjectEdgePath(HeapNodeModel source, double rowY, HeapNodeModel target)
        {
            double sourceRight = source.X + source.Width;
            if (target.X >= sourceRight + 20) return ForwardPath(sourceRight, rowY, target);

            double targetRight = target.X + target.Width;
            double y2 = target.Y + HeapMetrics.HeaderHeight / 2;
            if (targetRight + 20 <= source.X)
            {
                double dx = Math.Max(40, (source.X - targetRight) / 2);
                return Bezier(source.X, rowY, source.X - dx, targetRight + dx, targetRight, y2, arrowFromRight: true);
            }

            double c = Math.Max(sourceRight, targetRight) + 64;
            return Bezier(sourceRight, rowY, c, c, targetRight, y2, arrowFromRight: true);
        }

        /// <summary>Вперёд: из точки (x1,y1) слева направо в заголовок цели.</summary>
        private static string ForwardPath(double x1, double y1, HeapNodeModel target)
        {
            double y2 = target.Y + HeapMetrics.HeaderHeight / 2;
            double dx = Math.Max(40, (target.X - x1) / 2);
            return Bezier(x1, y1, x1 + dx, target.X - dx, target.X, y2, arrowFromRight: false);
        }

        /// <summary>Кубическая кривая Безье + стрелка-«шеврон» в конечной точке (синтаксис Path.Data).</summary>
        private static string Bezier(double x1, double y1, double c1x, double c2x, double x2, double y2, bool arrowFromRight)
        {
            double ax = arrowFromRight ? x2 + 8 : x2 - 8;  // стрелка «приходит» со стороны подхода кривой
            return string.Create(CultureInfo.InvariantCulture,
                $"M {x1:0.#},{y1:0.#} C {c1x:0.#},{y1:0.#} {c2x:0.#},{y2:0.#} {x2:0.#},{y2:0.#} " +
                $"M {ax:0.#},{y2 - 5:0.#} L {x2:0.#},{y2:0.#} L {ax:0.#},{y2 + 5:0.#}");
        }
    }
}
