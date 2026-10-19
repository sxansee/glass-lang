// «Что это за элемент кода?» — карточка по правой кнопке мыши в редакторе: ключевое слово, тип, встроенная функция,
// метод, переменная (с типом), функция или mold из ВАШЕЙ программы, число, строка. Работает по тексту, без запуска
// компилятора, поэтому отвечает мгновенно и на недописанном коде.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace Glass.Studio.CodeInfo
{
    public sealed record ElementInfo(string Title, string KindText, string Description, string? Example, string? Origin, string? DocTopic, int Start, int Length)
    {
        /// <summary>Ссылка на страницу документации (если для элемента она есть).</summary>
        public string? DocUrl => DocTopic == null ? null : AppInfo.DocsUrl + DocTopic;
    }

    public static class CodeInfoProvider
    {
        private static readonly Dictionary<string, string> Operators = new Dictionary<string, string>
        {
            ["+"] = "Сложение чисел. Со строкой — склейка (число или bool рядом со строкой превращается в текст); между массивами — склейка в новый массив.",
            ["-"] = "Вычитание или смена знака числа.",
            ["*"] = "Умножение чисел. Строка * число — повтор строки: \"ab\" * 3 даёт \"ababab\".",
            ["/"] = "Деление. Целые числа делятся нацело (7 / 2 == 3); чтобы получить дробный результат, одно из чисел должно быть double (7.0 / 2 == 3.5).",
            ["%"] = "Остаток от деления: 7 % 3 == 1. Удобно для проверки чётности: n % 2 == 0.",
            ["=="] = "Равно. Строки сравниваются по содержимому, числа — по значению (2 == 2.0), объекты — по ссылке.",
            ["!="] = "Не равно.",
            ["<"] = "Меньше. Для строк — по алфавиту (по порядку символов).",
            [">"] = "Больше. Для строк — по алфавиту.",
            ["<="] = "Меньше или равно.",
            [">="] = "Больше или равно.",
            ["&&"] = "Логическое «и»: истинно, если истинны оба условия. Правая часть не вычисляется, если левая ложна.",
            ["||"] = "Логическое «или»: истинно, если истинно хотя бы одно условие.",
            ["!"] = "Логическое «не»: меняет true на false и наоборот.",
            ["="] = "Присваивание. Тип нового значения должен подходить к типу переменной (int можно положить в double, обратного нельзя).",
            ["+="] = "Прибавить и присвоить: x += 5 то же, что x = x + 5. Работает и со строками, и с массивами.",
            ["-="] = "Вычесть и присвоить.",
            ["*="] = "Умножить и присвоить (для строки — повторить).",
            ["/="] = "Разделить и присвоить.",
            [".."] = "Диапазон в цикле: for i in 0..5 перебирает 0, 1, 2, 3, 4 (правая граница не входит). То же, что range(0, 5).",
            ["->"] = "Стрелка перед типом результата функции: fn square(x: int) -> int.",
            [":"] = "Двоеточие: тип после имени (let x: int), наследование (mold Dog : Animal), граница среза (xs[1:3]).",
            ["."] = "Доступ к полю или методу объекта, массива или строки: p.x, xs.length, s.upper().",
        };

        /// <param name="offset">Смещение символа в тексте (например, под указателем мыши).</param>
        public static ElementInfo? Describe(string source, int offset)
        {
            if (string.IsNullOrEmpty(source) || offset < 0 || offset > source.Length) return null;
            IReadOnlyList<SourceToken> tokens = SourceScanner.Scan(source);
            int index = FindToken(source, tokens, offset);
            if (index < 0) return null;

            SourceToken token = tokens[index];
            string text = token.Text(source);
            switch (token.Kind)
            {
                case TokenKind.Comment:
                    return new ElementInfo("Комментарий", "комментарий", "Пояснение для людей: всё от // до конца строки компилятор пропускает. Чтобы временно отключить строку кода, закомментируйте её.", null, null, "language/basics.md", token.Start, token.Length);
                case TokenKind.String:
                    return DescribeString(text, token);
                case TokenKind.Number:
                    bool isDouble = text.IndexOfAny(new[] { '.', 'e', 'E' }) >= 0;
                    return new ElementInfo(text, isDouble ? "число double" : "число int",
                        isDouble ? "Число с дробной частью (double). Печатается всегда с точкой: 3.0, 2.5." : "Целое число (int). Деление целых нацело: 7 / 2 == 3.",
                        null, null, "language/basics.md", token.Start, token.Length);
                case TokenKind.Keyword:
                case TokenKind.Introspection:
                    return FromReference(LanguageReference.Find(text).Where(e => e.Kind != ReferenceKind.Function || token.Kind == TokenKind.Keyword).ToList(), token, text);
                case TokenKind.Operator:
                    return Operators.TryGetValue(text, out string? description)
                        ? new ElementInfo(text, "оператор", description, null, null, "language/basics.md", token.Start, token.Length)
                        : null;
                default:
                    return DescribeIdentifier(source, tokens, new SymbolIndex(source, tokens), index);
            }
        }

        private static int FindToken(string source, IReadOnlyList<SourceToken> tokens, int offset)
        {
            for (int i = 0; i < tokens.Count; i++)
            {
                if (tokens[i].Start <= offset && offset < tokens[i].End) return i;
            }
            // Курсор сразу ЗА словом (конец строки, пробел): берём слово слева.
            bool onBlank = offset >= source.Length || char.IsWhiteSpace(source[offset]);
            if (onBlank)
            {
                for (int i = 0; i < tokens.Count; i++)
                {
                    if (tokens[i].End == offset) return i;
                }
            }
            return -1;
        }

        private static ElementInfo DescribeString(string literal, SourceToken token)
        {
            int length = 0;
            string content = literal.Length >= 2 && literal.EndsWith("\"", StringComparison.Ordinal) ? literal.Substring(1, literal.Length - 2) : literal.TrimStart('"');
            for (int i = 0; i < content.Length; i++)
            {
                if (content[i] == '\\' && i + 1 < content.Length) i++;
                if (!char.IsLowSurrogate(content[i])) length++;
            }
            string shown = literal.Length > 40 ? literal.Substring(0, 40) + "…" : literal;
            return new ElementInfo(shown, "строка", "Строка (string): " + length.ToString(CultureInfo.InvariantCulture) + " " + Plural(length, "символ", "символа", "символов") +
                ". Спецсимволы: \\n — перевод строки, \\t — табуляция, \\\" — кавычка, \\\\ — обратная косая черта.", null, null, "language/strings.md", token.Start, token.Length);
        }

        private static string Plural(int n, string one, string few, string many)
        {
            int last2 = n % 100, last = n % 10;
            return last2 >= 11 && last2 <= 14 ? many : last == 1 ? one : (last >= 2 && last <= 4) ? few : many;
        }

        /// <summary>Одна запись — как есть; несколько (int — тип и функция, contains — у массива и строки) — склеиваем.</summary>
        private static ElementInfo? FromReference(IReadOnlyList<ReferenceEntry> entries, SourceToken token, string name)
        {
            if (entries.Count == 0) return null;
            if (entries.Count == 1)
            {
                ReferenceEntry e = entries[0];
                return new ElementInfo(e.Signature, e.KindText, e.Description, e.Example, "встроено в язык", e.DocTopic, token.Start, token.Length);
            }
            string kinds = string.Join(" / ", entries.Select(e => e.KindText).Distinct());
            string description = string.Join("\n", entries.Select(e => e.KindText + ": " + e.Description));
            string title = entries.Select(e => e.Signature).Distinct().Count() == 1 ? entries[0].Signature : name;
            return new ElementInfo(title, kinds, description, entries.Select(e => e.Example).FirstOrDefault(x => x != null), "встроено в язык", entries[0].DocTopic, token.Start, token.Length);
        }

        // ------------------------------------------------------------------ идентификаторы

        private static ElementInfo? DescribeIdentifier(string source, IReadOnlyList<SourceToken> tokens, SymbolIndex symbols, int index)
        {
            SourceToken token = tokens[index];
            string name = token.Text(source);
            bool afterDot = index > 0 && tokens[index - 1].Kind == TokenKind.Operator && tokens[index - 1].Text(source) == ".";
            bool isCall = index + 1 < tokens.Count && tokens[index + 1].Kind == TokenKind.Operator && tokens[index + 1].Text(source) == "(";

            if (afterDot) return DescribeMember(source, tokens, symbols, index, name, token);

            // Имя в самом объявлении: «fn имя(...)» — метод mold или функция.
            if (index > 0 && tokens[index - 1].Kind == TokenKind.Keyword && tokens[index - 1].Text(source) == "fn")
            {
                FunctionSymbol? declared = symbols.Molds.SelectMany(m => m.Methods).FirstOrDefault(m => m.Name == name && m.Line == token.Line)
                                           ?? symbols.FindFunction(name);
                if (declared != null) return FunctionInfo(declared, token, declared.Owner == null ? "функция" : "метод");
            }

            if (isCall)
            {
                FunctionSymbol? fn = symbols.FindFunction(name);
                if (fn != null) return FunctionInfo(fn, token, "функция");
                MoldSymbol? constructed = symbols.FindMold(name);
                if (constructed != null)
                {
                    string signature = constructed.Constructor?.Signature ?? name + "()";
                    return new ElementInfo(signature, "конструктор", "Создаёт объект mold " + name + ". Слова new в Glass нет — объект создаётся вызовом по имени.",
                        null, "объявлено в строке " + constructed.Line.ToString(CultureInfo.InvariantCulture), "language/objects.md", token.Start, token.Length);
                }
                IReadOnlyList<ReferenceEntry> builtins = LanguageReference.Find(name).Where(e => e.Kind == ReferenceKind.Function).ToList();
                if (builtins.Count > 0) return FromReference(builtins, token, name);
            }

            VariableSymbol? variable = symbols.FindVariable(name, index);
            if (variable != null) return VariableInfo(variable, token);

            FunctionSymbol? owner = symbols.EnclosingFunction(index);
            if (owner?.Owner != null)
            {
                var member = symbols.FindMember(owner.Owner, name);
                if (member?.Field != null) return FieldInfo(member.Value.Owner, member.Value.Field!, token);
            }

            FunctionSymbol? function = symbols.FindFunction(name);
            if (function != null) return FunctionInfo(function, token, "функция");
            MoldSymbol? mold = symbols.FindMold(name);
            if (mold != null) return MoldInfo(mold, token);

            IReadOnlyList<ReferenceEntry> reference = LanguageReference.Find(name).Where(e => e.Kind == ReferenceKind.Constant || e.Kind == ReferenceKind.Function).ToList();
            if (reference.Count > 0) return FromReference(reference, token, name);

            return new ElementInfo(name, "имя", "Не нашёл объявления этого имени выше по тексту. Проверьте написание: возможно, опечатка, или переменная объявлена в другой функции.",
                null, null, "language/basics.md", token.Start, token.Length);
        }

        private static ElementInfo? DescribeMember(string source, IReadOnlyList<SourceToken> tokens, SymbolIndex symbols, int index, string name, SourceToken token)
        {
            string? receiverType = null;
            if (index >= 2 && tokens[index - 2].Kind == TokenKind.Identifier)
            {
                VariableSymbol? receiver = symbols.FindVariable(tokens[index - 2].Text(source), index - 2);
                receiverType = receiver?.Type;
            }
            else if (index >= 2 && tokens[index - 2].Kind == TokenKind.Keyword && tokens[index - 2].Text(source) == "this")
            {
                receiverType = symbols.EnclosingFunction(index)?.Owner;
            }
            else if (index >= 2 && tokens[index - 2].Kind == TokenKind.String)
            {
                receiverType = "string";
            }

            if (receiverType != null && symbols.FindMold(receiverType) != null)
            {
                var member = symbols.FindMember(receiverType, name);
                if (member != null)
                {
                    return member.Value.Field != null ? FieldInfo(member.Value.Owner, member.Value.Field, token) : FunctionInfo(member.Value.Method!, token, "метод");
                }
            }

            bool isArray = receiverType != null && receiverType.StartsWith("[", StringComparison.Ordinal);
            bool isString = receiverType == "string";
            IEnumerable<ReferenceEntry> candidates = LanguageReference.Find(name).Where(e =>
                e.Kind == ReferenceKind.Property ||
                (e.Kind == ReferenceKind.ArrayMethod && (isArray || (!isString && receiverType == null))) ||
                (e.Kind == ReferenceKind.StringMethod && (isString || (!isArray && receiverType == null))));
            List<ReferenceEntry> found = candidates.ToList();
            if (found.Count > 0) return FromReference(found, token, name);

            // Тип получателя неизвестен: поищем одноимённый член у любого mold программы.
            foreach (MoldSymbol mold in symbols.Molds)
            {
                var member = symbols.FindMember(mold.Name, name);
                if (member != null) return member.Value.Field != null ? FieldInfo(member.Value.Owner, member.Value.Field, token) : FunctionInfo(member.Value.Method!, token, "метод");
            }
            return new ElementInfo(name, "член", "Не нашёл поле или метод с таким именем" + (receiverType != null ? " у типа «" + receiverType + "»" : "") + ". Проверьте написание.",
                null, null, "language/objects.md", token.Start, token.Length);
        }

        private static ElementInfo FunctionInfo(FunctionSymbol fn, SourceToken token, string kind)
        {
            string description = fn.Owner == null
                ? "Функция из вашей программы."
                : "Метод mold " + fn.Owner + ".";
            if (fn.ReturnType != "void") description += " Возвращает " + fn.ReturnType + ".";
            else description += " Ничего не возвращает.";
            return new ElementInfo(fn.Signature, kind, description, null, "объявлено в строке " + fn.Line.ToString(CultureInfo.InvariantCulture),
                fn.Owner == null ? "language/functions.md" : "language/objects.md", token.Start, token.Length);
        }

        private static ElementInfo FieldInfo(MoldSymbol owner, FieldSymbol field, SourceToken token) =>
            new ElementInfo(field.Name + (field.Type.Length > 0 ? ": " + field.Type : ""), "поле",
                "Поле mold " + owner.Name + ": у каждого объекта своё значение.", null,
                "объявлено в строке " + field.Line.ToString(CultureInfo.InvariantCulture), "language/objects.md", token.Start, token.Length);

        private static ElementInfo VariableInfo(VariableSymbol v, SourceToken token)
        {
            string kind = v.Kind switch
            {
                VariableKind.Parameter => "параметр",
                VariableKind.LoopVariable => "переменная цикла",
                VariableKind.CatchVariable => "исключение",
                _ => "переменная",
            };
            var description = new StringBuilder();
            description.Append(v.Kind switch
            {
                VariableKind.Parameter => "Параметр функции" + (v.Scope != null ? " " + v.Scope.Name : "") + ".",
                VariableKind.LoopVariable => "Переменная цикла for: на каждом шаге получает следующий элемент.",
                VariableKind.CatchVariable => "Перехваченное исключение; у него есть поле message.",
                _ => "Переменная, объявленная в строке " + v.Line.ToString(CultureInfo.InvariantCulture) + ".",
            });
            if (v.IsInferred && v.Kind == VariableKind.Local) description.Append(" Тип выведен из значения и дальше не меняется.");
            string title = v.Name + ": " + v.Type;
            return new ElementInfo(title, kind, description.ToString(), null, null, "language/basics.md", token.Start, token.Length);
        }

        private static ElementInfo MoldInfo(MoldSymbol mold, SourceToken token)
        {
            var description = new StringBuilder();
            description.Append(mold.IsFacet ? "Контракт (facet): методы без реализации." : "Тип объектов (mold).");
            if (mold.Base != null) description.Append(" Наследует ").Append(mold.Base).Append('.');
            if (mold.Facets.Count > 0) description.Append(" Реализует: ").Append(string.Join(", ", mold.Facets)).Append('.');
            if (mold.Fields.Count > 0) description.Append("\nПоля: ").Append(string.Join(", ", mold.Fields.Select(f => f.Name + (f.Type.Length > 0 ? ": " + f.Type : "")))).Append('.');
            if (mold.Methods.Count > 0) description.Append("\nМетоды: ").Append(string.Join(", ", mold.Methods.Select(m => m.Name + "()"))).Append('.');
            return new ElementInfo((mold.IsFacet ? "facet " : "mold ") + mold.Name, mold.IsFacet ? "facet" : "mold", description.ToString(), null,
                "объявлено в строке " + mold.Line.ToString(CultureInfo.InvariantCulture), "language/objects.md", token.Start, token.Length);
        }
    }
}
