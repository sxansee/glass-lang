// Статический линтер XAML для WPF-оболочки.
//
// ЗАЧЕМ. WPF в контейнере, где писался проект, не собрать (нет Windows и пакетов), а XAML — это не
// «просто разметка»: опечатка в имени свойства в {Binding ...}, забытый Mode=OneWay, привязка bool к
// Visibility без конвертера, ссылка на несуществующий ресурс или обработчик не ловятся компилятором
// и проявляются уже в работающем окне — привязки молча не работают, а часть ошибок (TwoWay к свойству
// без set) бросает исключение при показе окна. Линтер проверяет такие вещи заранее — по тем же
// ViewModel-типам, на которые XAML будет смотреть в работе (reflection по Glass.Studio.Core).
//
// ЧТО ПРОВЕРЯЕТ: well-formed XML; пути {Binding ...} от типа DataContext (d:DataContext, DataType
// шаблонов, ItemsSource → тип элемента, DataContext="{Binding ...}", стили — по месту их применения);
// права записи для TwoWay (в том числе TwoWay по умолчанию: TextBox.Text, SelectedItem, Run.Text...);
// bool→Visibility без Converter; Command — ICommand; ItemsSource — коллекция; литералы в DataTrigger
// против типа свойства; StaticResource/DynamicResource по областям видимости и порядку объявления;
// x:Static и x:Type; обработчики событий в code-behind (имя и сигнатура); дубли x:Name/x:Key;
// свойство, заданное и атрибутом, и элементом свойства.
// ЧЕГО НЕ ПРОВЕРЯЕТ: типы самого WPF (Button, Grid...) и их свойства — их reflection здесь нет.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;
using System.Windows.Input;

namespace Glass.Studio.Tests
{
    internal sealed class Markup
    {
        public string Name = "";
        public readonly List<string> Positional = new List<string>();
        public readonly Dictionary<string, string> Named = new Dictionary<string, string>();

        public string LocalName
        {
            get
            {
                int colon = Name.IndexOf(':');
                return colon >= 0 ? Name.Substring(colon + 1) : Name;
            }
        }

        public static Markup? Parse(string value)
        {
            string text = value.Trim();
            if (text.Length < 2 || text[0] != '{' || text[text.Length - 1] != '}') return null;
            if (text.StartsWith("{}", StringComparison.Ordinal)) return null;   // экранирование: литерал, не расширение

            string inner = text.Substring(1, text.Length - 2).Trim();
            int i = 0;
            while (i < inner.Length && !char.IsWhiteSpace(inner[i]) && inner[i] != ',') i++;
            var markup = new Markup { Name = inner.Substring(0, i) };
            string rest = inner.Substring(i).TrimStart(' ', '\t', '\r', '\n', ',');

            foreach (string part in SplitTopLevel(rest))
            {
                string piece = part.Trim();
                if (piece.Length == 0) continue;
                int eq = IndexOfTopLevel(piece, '=');
                if (eq > 0 && piece[0] != '{') markup.Named[piece.Substring(0, eq).Trim()] = piece.Substring(eq + 1).Trim();
                else markup.Positional.Add(piece);
            }
            return markup;
        }

        private static IEnumerable<string> SplitTopLevel(string text)
        {
            int depth = 0, start = 0;
            char quote = '\0';
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (quote != '\0') { if (c == quote) quote = '\0'; continue; }
                if (c == '\'' || c == '"') quote = c;
                else if (c == '{') depth++;
                else if (c == '}') depth--;
                else if (c == ',' && depth == 0)
                {
                    yield return text.Substring(start, i - start);
                    start = i + 1;
                }
            }
            if (start <= text.Length) yield return text.Substring(start);
        }

        private static int IndexOfTopLevel(string text, char target)
        {
            int depth = 0;
            for (int i = 0; i < text.Length; i++)
            {
                if (text[i] == '{') depth++;
                else if (text[i] == '}') depth--;
                else if (text[i] == target && depth == 0) return i;
            }
            return -1;
        }
    }

    public sealed class XamlLinter
    {
        private const string XamlNs = "http://schemas.microsoft.com/winfx/2006/xaml";
        private const string DesignNs = "http://schemas.microsoft.com/expression/blend/2008";
        private const string WpfNs = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

        // Свойства, у которых привязка по умолчанию TwoWay: без Mode=OneWay на свойстве без set они падают при показе окна.
        private static readonly HashSet<string> TwoWayByDefault = new HashSet<string>
        {
            "TextBox.Text", "Run.Text", "ComboBox.Text", "Slider.Value", "ScrollBar.Value",
            "CheckBox.IsChecked", "RadioButton.IsChecked", "ToggleButton.IsChecked", "MenuItem.IsChecked",
            "Expander.IsExpanded", "ListBoxItem.IsSelected", "TreeViewItem.IsExpanded", "TreeViewItem.IsSelected",
            "ListBox.SelectedItem", "ListBox.SelectedIndex", "ListBox.SelectedValue",
            "ListView.SelectedItem", "ListView.SelectedIndex", "ListView.SelectedValue",
            "ComboBox.SelectedItem", "ComboBox.SelectedIndex", "ComboBox.SelectedValue",
            "TabControl.SelectedItem", "TabControl.SelectedIndex", "TabControl.SelectedValue",
            "DataGrid.SelectedItem", "DataGrid.SelectedIndex",
        };

        private static readonly HashSet<string> EventNames = new HashSet<string>
        {
            "Click", "Closing", "Closed", "Loaded", "Unloaded", "SizeChanged", "TextChanged", "SelectionChanged",
            "MouseDown", "MouseUp", "MouseMove", "MouseEnter", "MouseLeave", "MouseDoubleClick", "MouseWheel",
            "MouseLeftButtonDown", "MouseLeftButtonUp", "MouseRightButtonDown", "MouseRightButtonUp",
            "PreviewMouseDown", "PreviewMouseUp", "PreviewMouseWheel", "PreviewMouseLeftButtonDown", "PreviewMouseLeftButtonUp",
            "KeyDown", "KeyUp", "PreviewKeyDown", "PreviewKeyUp", "GotFocus", "LostFocus", "Checked", "Unchecked",
            "Drop", "DragOver", "ContextMenuOpening", "Initialized", "ValueChanged", "ScrollChanged",
        };

        private readonly Assembly _core;
        private readonly Func<string, bool> _localTypeExists;
        private readonly Func<string, string, bool>? _localAttachedPropertyExists;
        private readonly HashSet<string> _appKeys;
        private readonly List<Type> _knownTypes;

        public int BindingsChecked { get; private set; }
        public int ResourceRefsChecked { get; private set; }
        public int HandlersChecked { get; private set; }
        public int StaticsChecked { get; private set; }

        /// <param name="coreAssembly">Glass.Studio.Core — откуда берутся типы ViewModel/моделей.</param>
        /// <param name="localTypeExists">Есть ли в самом WPF-проекте класс с таким именем (его reflection недоступна).</param>
        /// <param name="appResourceKeys">Ключи словарей уровня приложения (App.xaml → Themes/Glass.xaml).</param>
        /// <param name="localAttachedPropertyExists">
        /// Есть ли в WPF-проекте у класса-владельца (первый аргумент) attached-свойство с таким именем (второй):
        /// RegisterAttached("Имя", ...). Без него существование свойств вида beh:Класс.Имя не проверяется.
        /// </param>
        public XamlLinter(Assembly coreAssembly, Func<string, bool> localTypeExists, IEnumerable<string> appResourceKeys,
                          Func<string, string, bool>? localAttachedPropertyExists = null)
        {
            _core = coreAssembly;
            _localTypeExists = localTypeExists;
            _localAttachedPropertyExists = localAttachedPropertyExists;
            _appKeys = new HashSet<string>(appResourceKeys);
            string[] namespaces = { "Glass.Studio.ViewModels", "Glass.Studio.Heap", "Glass.Studio.Models", "Glass.Studio.Examples" };
            _knownTypes = _core.GetExportedTypes().Where(t => t.Namespace != null && namespaces.Contains(t.Namespace) && !t.IsAbstract || t.IsEnum).ToList();
        }

        /// <summary>Ключи (x:Key) прямых потомков словаря ресурсов.</summary>
        public static IEnumerable<string> KeysOf(XElement dictionaryRoot)
        {
            foreach (XElement child in dictionaryRoot.Elements())
            {
                string? key = (string?)child.Attribute(XName.Get("Key", XamlNs));
                if (key != null) yield return key;
            }
        }

        // ------------------------------------------------------------------ состояние одного файла

        private sealed class FileState
        {
            public string File = "";
            public string? CodeBehind;
            public bool Report;
            public readonly List<string> Errors = new List<string>();
            public readonly Dictionary<string, List<Type[]?>> StyleUsage = new Dictionary<string, List<Type[]?>>();
            public readonly HashSet<string> Names = new HashSet<string>();
        }

        public IReadOnlyList<string> Lint(string fileName, XDocument document, string? codeBehind)
        {
            var state = new FileState { File = fileName, CodeBehind = codeBehind };
            XElement root = document.Root ?? throw new InvalidOperationException("Пустой документ: " + fileName);

            // Проход 1 — собрать, где применяются стили-ресурсы (их привязки определены контекстом применения).
            state.Report = false;
            Walk(root, null, state);

            // Проход 2 — проверка.
            state.Report = true;
            state.Names.Clear();
            Walk(root, null, state);
            return state.Errors;
        }

        private static string Where(XElement el) =>
            el is IXmlLineInfo info && info.HasLineInfo() ? ":" + info.LineNumber : "";

        private void Error(FileState st, XElement el, string message)
        {
            if (st.Report) st.Errors.Add(st.File + Where(el) + ": " + message);
        }

        // ------------------------------------------------------------------ обход

        private void Walk(XElement el, Type[]? inheritedContext, FileState st)
        {
            string local = el.Name.LocalName;
            Type[]? effective = inheritedContext;

            // DataContext="{Binding Путь}" на элементе меняет контекст его самого и потомков;
            // сама привязка DataContext вычисляется в ещё прежнем контексте.
            XAttribute? dataContextAttr = el.Attribute("DataContext");
            if (dataContextAttr != null)
            {
                Markup? m = Markup.Parse(dataContextAttr.Value);
                effective = m != null && m.LocalName == "Binding" ? ResolveBindingTypes(el, m, inheritedContext, st, countAsBinding: false) : null;
            }

            XAttribute? design = el.Attribute(XName.Get("DataContext", DesignNs));
            if (design != null)
            {
                Markup? m = Markup.Parse(design.Value);
                string? typeToken = m == null ? null : (m.Named.TryGetValue("Type", out string? t) ? t : m.Positional.FirstOrDefault());
                Type? type = typeToken == null ? null : ResolveTypeToken(el, typeToken, st, report: false);
                effective = type == null ? null : new[] { type };
            }

            if (local == "DataTemplate")
            {
                string? dataType = (string?)el.Attribute("DataType");
                Markup? m = dataType == null ? null : Markup.Parse(dataType);
                if (m != null && m.LocalName == "Type" && m.Positional.Count == 1)
                {
                    Type? type = ResolveTypeToken(el, m.Positional[0], st, report: true);
                    effective = type == null ? null : new[] { type };
                }
            }

            // Стиль-ресурс: контекст его привязок — где он применяется.
            if (local == "Style" && el.Parent != null && IsResourceDictionary(el.Parent))
            {
                string? key = (string?)el.Attribute(XName.Get("Key", XamlNs));
                effective = key != null && st.StyleUsage.TryGetValue(key, out List<Type[]?>? usages) && usages.All(u => u != null)
                    ? usages.SelectMany(u => u!).Distinct().Select(t => new[] { t }).SelectMany(x => x).ToArray()
                    : null;
            }

            if (st.Report) CheckDuplicates(el, st);

            // Атрибуты.
            Type[]? itemContext = null;
            bool itemContextComputed = false;
            foreach (XAttribute attr in el.Attributes())
            {
                if (attr.IsNamespaceDeclaration) continue;
                string attrName = attr.Name.LocalName;

                if (attr.Name.Namespace == XamlNs)
                {
                    if (attrName == "Name" && st.Report && !st.Names.Add(attr.Value)) Error(st, el, "x:Name «" + attr.Value + "» объявлено дважды");
                    continue;
                }
                if (attr.Name.Namespace == DesignNs) continue;
                if (attr.Name.Namespace != XNamespace.None)
                {
                    CheckPrefixedAttribute(el, attr, effective, st);
                    continue;
                }

                if (st.Report && EventNames.Contains(attrName) && Markup.Parse(attr.Value) == null) CheckHandler(el, attr, st);

                Markup? markup = Markup.Parse(attr.Value);
                if (markup == null) continue;

                switch (markup.LocalName)
                {
                    case "Binding":
                        if (attrName == "DataContext") break;   // уже обработано выше
                        CheckBinding(el, attr, markup, effective, st);
                        if (attrName == "ItemsSource" && !itemContextComputed)
                        {
                            itemContext = ItemContext(el, markup, effective, st);
                            itemContextComputed = true;
                        }
                        break;
                    case "StaticResource":
                    case "DynamicResource":
                        CheckResourceRef(el, attr, markup, st);
                        if (markup.Positional.Count == 1 && attrName == "Style")
                        {
                            AddStyleUsage(st, markup.Positional[0], effective);
                        }
                        else if (markup.Positional.Count == 1 && attrName == "ItemContainerStyle")
                        {
                            if (!itemContextComputed)
                            {
                                XAttribute? source = el.Attribute("ItemsSource");
                                Markup? sm = source == null ? null : Markup.Parse(source.Value);
                                itemContext = sm != null && sm.LocalName == "Binding" ? ItemContext(el, sm, effective, st) : null;
                                itemContextComputed = true;
                            }
                            AddStyleUsage(st, markup.Positional[0], itemContext);
                        }
                        break;
                    case "Static":
                        CheckStatic(el, markup, st);
                        break;
                    case "Type":
                        if (markup.Positional.Count == 1) ResolveTypeToken(el, markup.Positional[0], st, report: true);
                        break;
                }
            }

            // DataTrigger: литерал Value должен подходить к типу привязанного свойства.
            if (st.Report && local == "DataTrigger") CheckDataTriggerLiteral(el, effective, st);

            if (!itemContextComputed)
            {
                XAttribute? source = el.Attribute("ItemsSource");
                Markup? sm = source == null ? null : Markup.Parse(source.Value);
                if (sm != null && sm.LocalName == "Binding") itemContext = ItemContext(el, sm, effective, st);
            }

            // Потомки.
            foreach (XElement child in el.Elements())
            {
                string childLocal = child.Name.LocalName;
                Type[]? childContext = effective;

                if (childLocal.EndsWith(".Resources", StringComparison.Ordinal)) childContext = null;
                else if (childLocal.EndsWith(".ItemTemplate", StringComparison.Ordinal) ||
                         childLocal.EndsWith(".ItemContainerStyle", StringComparison.Ordinal) ||
                         childLocal.EndsWith(".View", StringComparison.Ordinal))
                {
                    childContext = itemContext;
                    if (st.Report && childLocal.EndsWith(".ItemTemplate", StringComparison.Ordinal)) CheckTemplateType(child, itemContext, st);
                }

                Walk(child, childContext, st);
            }
        }

        private static bool IsResourceDictionary(XElement parent) =>
            parent.Name.LocalName == "ResourceDictionary" || parent.Name.LocalName.EndsWith(".Resources", StringComparison.Ordinal);

        private static void AddStyleUsage(FileState st, string key, Type[]? context)
        {
            if (!st.StyleUsage.TryGetValue(key, out List<Type[]?>? list)) st.StyleUsage[key] = list = new List<Type[]?>();
            list.Add(context);
        }

        // ------------------------------------------------------------------ дубли

        private void CheckDuplicates(XElement el, FileState st)
        {
            foreach (XAttribute attr in el.Attributes())
            {
                if (attr.IsNamespaceDeclaration || attr.Name.Namespace != XNamespace.None) continue;
                string propertyElement = el.Name.LocalName + "." + attr.Name.LocalName;
                if (el.Elements().Any(c => c.Name.LocalName == propertyElement))
                {
                    Error(st, el, "свойство «" + attr.Name.LocalName + "» задано и атрибутом, и элементом <" + propertyElement + ">");
                }
            }

            // Ключи словаря ресурсов — без повторов.
            if (IsResourceDictionary(el))
            {
                foreach (IGrouping<string, string> group in KeysOf(el).GroupBy(k => k).Where(g => g.Count() > 1))
                {
                    Error(st, el, "ключ ресурса «" + group.Key + "» объявлен несколько раз в одном словаре");
                }
            }
        }

        // ------------------------------------------------------------------ обработчики

        private void CheckHandler(XElement el, XAttribute attr, FileState st)
        {
            HandlersChecked++;
            string name = attr.Value.Trim();
            if (st.CodeBehind == null)
            {
                Error(st, el, "обработчик «" + name + "» (" + attr.Name.LocalName + "), но у файла нет code-behind");
                return;
            }
            Match declaration = Regex.Match(st.CodeBehind, @"\bvoid\s+" + Regex.Escape(name) + @"\s*\(([^)]*)\)");
            if (!declaration.Success)
            {
                Error(st, el, "обработчик «" + name + "» (" + attr.Name.LocalName + ") не найден в code-behind");
                return;
            }
            string parameters = declaration.Groups[1].Value.Trim();
            if (!Regex.IsMatch(parameters, @"^object\??\s+\w+\s*,\s*[\w.]+\??\s+\w+$"))
            {
                Error(st, el, "обработчик «" + name + "» должен иметь сигнатуру (object sender, XxxEventArgs e), а не (" + parameters + ")");
            }
        }

        // ------------------------------------------------------------------ привязки

        private void CheckBinding(XElement el, XAttribute attr, Markup markup, Type[]? context, FileState st)
        {
            ResolveBindingTypes(el, markup, context, st, countAsBinding: true, attr: attr);
        }

        /// <summary>Разбирает путь привязки, проверяет его и возвращает тип значения (null — не определить).</summary>
        private Type[]? ResolveBindingTypes(XElement el, Markup markup, Type[]? context, FileState st, bool countAsBinding, XAttribute? attr = null)
        {
            if (countAsBinding && st.Report) BindingsChecked++;

            bool external = markup.Named.ContainsKey("ElementName") || markup.Named.ContainsKey("RelativeSource") || markup.Named.ContainsKey("Source");
            string? path = markup.Named.TryGetValue("Path", out string? explicitPath) ? explicitPath : markup.Positional.FirstOrDefault();
            if (path != null) path = path.Trim();
            if (external) return null;

            Type[]? resolved;
            PropertyInfo? finalProperty = null;
            if (string.IsNullOrEmpty(path) || path == ".")
            {
                resolved = context;
            }
            else if (path.Contains('[') || path.Contains('('))
            {
                return null;   // индексаторы и вложенные свойства — не проверяем
            }
            else if (context == null)
            {
                // Контекст неизвестен (стиль-ресурс без применений и т.п.): хотя бы первое имя должно существовать где-то.
                string first = path.Split('.')[0];
                if (!_knownTypes.Any(t => FindProperty(t, first) != null))
                {
                    Error(st, el, "в привязке {Binding " + path + "}: нет свойства «" + first + "» ни в одном типе ViewModel");
                }
                return null;
            }
            else
            {
                resolved = null;
                var results = new List<Type>();
                foreach (Type start in context)
                {
                    Type current = start;
                    PropertyInfo? last = null;
                    bool ok = true;
                    foreach (string segment in path.Split('.'))
                    {
                        PropertyInfo? property = FindProperty(current, segment);
                        if (property == null)
                        {
                            Error(st, el, "в привязке {Binding " + path + "}: у типа " + current.Name + " нет публичного свойства «" + segment + "»");
                            ok = false;
                            break;
                        }
                        last = property;
                        current = property.PropertyType;
                    }
                    if (!ok) return null;
                    finalProperty = last;
                    results.Add(current);
                }
                resolved = results.ToArray();
            }

            if (attr == null || resolved == null || resolved.Length == 0) return resolved;

            string attrName = attr.Name.LocalName;
            string owner = el.Name.LocalName;
            Type valueType = resolved[0];

            string mode = markup.Named.TryGetValue("Mode", out string? explicitMode)
                ? explicitMode
                : (TwoWayByDefault.Contains(owner + "." + attrName) ? "TwoWay" : "OneWay");
            if ((mode == "TwoWay" || mode == "OneWayToSource") && finalProperty != null && finalProperty.SetMethod?.IsPublic != true)
            {
                Error(st, el, "привязка {Binding " + path + "} к " + owner + "." + attrName + " работает в режиме " + mode +
                              ", а у свойства «" + finalProperty.Name + "» нет публичного set — упадёт при показе окна (добавьте Mode=OneWay)");
            }

            if (attrName == "Visibility" && (valueType == typeof(bool) || valueType == typeof(bool?)) && !markup.Named.ContainsKey("Converter"))
            {
                Error(st, el, "bool-свойство «" + path + "» привязано к Visibility без Converter — работать не будет");
            }
            if (attrName == "Command" && !typeof(ICommand).IsAssignableFrom(valueType))
            {
                Error(st, el, "Command привязана к «" + path + "» типа " + valueType.Name + ", это не ICommand");
            }
            if (attrName == "ItemsSource" && (valueType == typeof(string) || !typeof(System.Collections.IEnumerable).IsAssignableFrom(valueType)))
            {
                Error(st, el, "ItemsSource привязан к «" + path + "» типа " + valueType.Name + ", это не коллекция");
            }
            return resolved;
        }

        private Type[]? ItemContext(XElement el, Markup itemsSource, Type[]? context, FileState st)
        {
            // Тип элементов — без повторного учёта привязки и без повторных сообщений об ошибках.
            bool previous = st.Report;
            st.Report = false;
            try
            {
                Type[]? sourceTypes = ResolveBindingTypes(el, itemsSource, context, st, countAsBinding: false);
                if (sourceTypes == null || sourceTypes.Length == 0) return null;
                Type? element = ElementTypeOf(sourceTypes[0]);
                return element == null ? null : new[] { element };
            }
            finally
            {
                st.Report = previous;
            }
        }

        private static Type? ElementTypeOf(Type collection)
        {
            if (collection == typeof(string)) return null;
            if (collection.IsArray) return collection.GetElementType();
            IEnumerable<Type> candidates = new[] { collection }.Concat(collection.GetInterfaces());
            foreach (Type candidate in candidates)
            {
                if (candidate.IsGenericType && candidate.GetGenericTypeDefinition() == typeof(IEnumerable<>)) return candidate.GetGenericArguments()[0];
            }
            return null;
        }

        private static PropertyInfo? FindProperty(Type type, string name)
        {
            PropertyInfo? property = type.GetProperty(name, BindingFlags.Public | BindingFlags.Instance | BindingFlags.FlattenHierarchy);
            if (property != null) return property;
            if (type.IsInterface)
            {
                foreach (Type parent in type.GetInterfaces())
                {
                    property = parent.GetProperty(name, BindingFlags.Public | BindingFlags.Instance);
                    if (property != null) return property;
                }
            }
            return null;
        }

        private void CheckTemplateType(XElement itemTemplateElement, Type[]? itemContext, FileState st)
        {
            if (itemContext == null || itemContext.Length != 1) return;
            XElement? template = itemTemplateElement.Elements().FirstOrDefault(e => e.Name.LocalName == "DataTemplate");
            string? dataType = (string?)template?.Attribute("DataType");
            Markup? m = dataType == null ? null : Markup.Parse(dataType);
            if (template == null || m == null || m.Positional.Count != 1) return;
            Type? declared = ResolveTypeToken(template, m.Positional[0], st, report: false);
            if (declared != null && !declared.IsAssignableFrom(itemContext[0]))
            {
                Error(st, template, "DataTemplate.DataType = " + declared.Name + ", но элементы ItemsSource имеют тип " + itemContext[0].Name);
            }
        }

        private void CheckDataTriggerLiteral(XElement trigger, Type[]? context, FileState st)
        {
            string? binding = (string?)trigger.Attribute("Binding");
            string? literal = (string?)trigger.Attribute("Value");
            Markup? m = binding == null ? null : Markup.Parse(binding);
            if (m == null || m.LocalName != "Binding" || literal == null || Markup.Parse(literal) != null || context == null) return;

            bool previous = st.Report;
            st.Report = false;   // сообщения о самом пути уже даны при проверке атрибута Binding
            Type[]? types;
            try { types = ResolveBindingTypes(trigger, m, context, st, countAsBinding: false); }
            finally { st.Report = previous; }
            if (types == null || types.Length == 0) return;

            Type t = Nullable.GetUnderlyingType(types[0]) ?? types[0];
            bool fits = true;
            string kind = "";
            if (t == typeof(bool)) { fits = literal == "True" || literal == "False" || literal == "true" || literal == "false"; kind = "bool (True/False)"; }
            else if (t == typeof(int) || t == typeof(long)) { fits = long.TryParse(literal, NumberStyles.Integer, CultureInfo.InvariantCulture, out _); kind = "целое число"; }
            else if (t == typeof(double)) { fits = double.TryParse(literal, NumberStyles.Float, CultureInfo.InvariantCulture, out _); kind = "число"; }
            else if (t.IsEnum) { fits = Enum.GetNames(t).Contains(literal); kind = "значение " + t.Name + " (" + string.Join("/", Enum.GetNames(t)) + ")"; }
            if (!fits) Error(st, trigger, "DataTrigger: Value=\"" + literal + "\" не подходит к типу " + t.Name + " — ожидается " + kind);
        }

        // ------------------------------------------------------------------ типы и статика

        /// <summary>«vm:MainViewModel» → Type. null — не найден (с ошибкой, если пространство имён проверяемое) или это тип WPF.</summary>
        // Атрибут с префиксом пространства имён — attached-свойство из собственного класса: beh:RichTextBehavior.Source="{Binding Text}".
        // Раньше такие атрибуты пропускались целиком, и опечатка в привязке молча давала пустое место на экране.
        private void CheckPrefixedAttribute(XElement el, XAttribute attr, Type[]? context, FileState st)
        {
            string local = attr.Name.LocalName;
            int dot = local.IndexOf('.');
            if (dot <= 0) return;   // не attached-свойство (mc:Ignorable, xml:space ...)
            Match uri = Regex.Match(attr.Name.NamespaceName, @"^clr-namespace:(?<ns>[^;]+)(;assembly=(?<asm>.+))?$");
            if (!uri.Success) return;   // чужие схемы (WPF, Blend) — не проверяем

            if (st.Report)
            {
                CheckAttachedProperty(el, local.Substring(0, dot), local.Substring(dot + 1), uri.Groups["ns"].Value,
                                      uri.Groups["asm"].Success ? uri.Groups["asm"].Value : "", st);
            }

            Markup? markup = Markup.Parse(attr.Value);
            if (markup == null) return;
            switch (markup.LocalName)
            {
                case "Binding":
                    CheckBinding(el, attr, markup, context, st);
                    break;
                case "StaticResource":
                case "DynamicResource":
                    CheckResourceRef(el, attr, markup, st);
                    break;
                case "Static":
                    CheckStatic(el, markup, st);
                    break;
            }
        }

        private void CheckAttachedProperty(XElement el, string owner, string property, string ns, string assembly, FileState st)
        {
            string full = owner + "." + property;
            if (assembly == "Glass.Studio.Core")
            {
                Type? type = _core.GetType(ns + "." + owner);
                if (type == null)
                {
                    Error(st, el, "attached-свойство «" + full + "»: тип " + ns + "." + owner + " не найден в Glass.Studio.Core");
                    return;
                }
                if (type.GetField(property + "Property", BindingFlags.Public | BindingFlags.Static | BindingFlags.FlattenHierarchy) == null)
                    Error(st, el, "attached-свойство «" + full + "»: у типа " + owner + " нет публичного статического поля " + property + "Property");
                return;
            }
            if (assembly == "" && ns.StartsWith("Glass.Studio", StringComparison.Ordinal) && _localAttachedPropertyExists != null)
            {
                if (!_localTypeExists(owner))
                {
                    Error(st, el, "attached-свойство «" + full + "»: тип " + owner + " не найден в исходниках Glass.Studio (WPF-проект)");
                    return;
                }
                if (!_localAttachedPropertyExists(owner, property))
                    Error(st, el, "attached-свойство «" + full + "» не найдено: в классе " + owner + " нет RegisterAttached(\"" + property + "\", ...)");
            }
        }

        private Type? ResolveTypeToken(XElement scope, string token, FileState st, bool report)
        {
            string prefix = "", name = token.Trim();
            int colon = name.IndexOf(':');
            if (colon > 0) { prefix = name.Substring(0, colon); name = name.Substring(colon + 1); }

            string? uri = prefix == "" ? scope.GetDefaultNamespace().NamespaceName : scope.GetNamespaceOfPrefix(prefix)?.NamespaceName;
            if (uri == null)
            {
                if (report) Error(st, scope, "неизвестный префикс пространства имён «" + prefix + "» в «" + token + "»");
                return null;
            }
            Match match = Regex.Match(uri, @"^clr-namespace:(?<ns>[^;]+)(;assembly=(?<asm>.+))?$");
            if (!match.Success) return null;   // типы WPF/AvalonEdit — не проверяем

            string ns = match.Groups["ns"].Value;
            string assembly = match.Groups["asm"].Success ? match.Groups["asm"].Value : "";
            if (assembly == "Glass.Studio.Core")
            {
                Type? type = _core.GetType(ns + "." + name);
                if (type == null && report) Error(st, scope, "тип " + ns + "." + name + " не найден в Glass.Studio.Core");
                return type;
            }
            if (assembly == "" && ns.StartsWith("Glass.Studio", StringComparison.Ordinal))
            {
                if (!_localTypeExists(name) && report) Error(st, scope, "тип " + name + " не найден в исходниках Glass.Studio (WPF-проект)");
                return null;
            }
            return null;
        }

        private void CheckStatic(XElement el, Markup markup, FileState st)
        {
            if (!st.Report || markup.Positional.Count != 1) return;
            StaticsChecked++;
            string reference = markup.Positional[0];
            int dot = reference.LastIndexOf('.');
            if (dot <= 0) { Error(st, el, "{x:Static " + reference + "}: ожидается Тип.Член"); return; }

            Type? type = ResolveTypeToken(el, reference.Substring(0, dot), st, report: true);
            if (type == null) return;   // тип WPF либо ошибка уже записана
            string member = reference.Substring(dot + 1);
            bool found = type.GetField(member, BindingFlags.Public | BindingFlags.Static) != null ||
                         type.GetProperty(member, BindingFlags.Public | BindingFlags.Static) != null;
            if (!found) Error(st, el, "{x:Static " + reference + "}: у типа " + type.Name + " нет публичного статического члена «" + member + "»");
        }

        // ------------------------------------------------------------------ ресурсы

        private void CheckResourceRef(XElement el, XAttribute attr, Markup markup, FileState st)
        {
            if (!st.Report) return;
            ResourceRefsChecked++;
            string? key = markup.Positional.FirstOrDefault() ?? (markup.Named.TryGetValue("ResourceKey", out string? named) ? named : null);
            if (key == null || key.StartsWith("{", StringComparison.Ordinal)) return;   // {DynamicResource {x:Static ...}} — системные ключи

            // Идём вверх по предкам: у каждого смотрим его Resources.
            bool insideTemplate = false;
            XElement? child = null;
            for (XElement? ancestor = el; ancestor != null; child = ancestor, ancestor = ancestor.Parent)
            {
                string aLocal = ancestor.Name.LocalName;
                if (aLocal == "DataTemplate" || aLocal == "ControlTemplate" || aLocal == "HierarchicalDataTemplate") insideTemplate = true;

                // Корень Themes/Glass.xaml — сам словарь.
                IEnumerable<XElement> dictionaries = ancestor.Name.LocalName == "ResourceDictionary" && ancestor.Parent == null
                    ? new[] { ancestor }
                    : ancestor.Elements().Where(e => e.Name.LocalName == aLocal + ".Resources");

                foreach (XElement resources in dictionaries)
                {
                    IEnumerable<XElement> entries = resources.Name.LocalName == "ResourceDictionary" && resources.Parent == null
                        ? resources.Elements()
                        : resources.Elements().SelectMany(e => e.Name.LocalName == "ResourceDictionary" ? e.Elements() : new[] { e });
                    List<XElement> list = entries.ToList();
                    int index = list.FindIndex(e => (string?)e.Attribute(XName.Get("Key", XamlNs)) == key);
                    if (index < 0) continue;

                    // StaticResource вне шаблона не видит определений НИЖЕ по тексту того же словаря.
                    if (markup.LocalName == "StaticResource" && !insideTemplate)
                    {
                        XElement? holder = child != null && list.Any(e => e == child || e.Descendants().Contains(child)) ? list.First(e => e == child || e.Descendants().Contains(child)) : null;
                        if (holder == null)
                        {
                            holder = list.FirstOrDefault(e => e.DescendantsAndSelf().Contains(el));
                        }
                        int holderIndex = holder == null ? -1 : list.IndexOf(holder);
                        if (holderIndex >= 0 && holderIndex <= index)
                        {
                            Error(st, el, "{StaticResource " + key + "}: ресурс объявлен ниже (или в нём же) в том же словаре — StaticResource не видит определений позже по тексту");
                        }
                    }
                    return;
                }
            }

            if (!_appKeys.Contains(key)) Error(st, el, "{" + markup.Name + " " + key + "}: ресурс не найден ни в словарях элемента и его предков, ни в App.xaml");
        }

        // ------------------------------------------------------------------ имена в тексте для тестов

        public static IEnumerable<XElement> Resources(XDocument document) =>
            document.Descendants().Where(e => e.Name.LocalName.EndsWith(".Resources", StringComparison.Ordinal));
    }
}
